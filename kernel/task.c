#include <rum/context.h>
#include <rum/cpu.h>
#include <rum/gdt.h>
#include <rum/interrupts.h>
#include <rum/abi/syscall.h>
#include <rum/memory.h>
#include <rum/process_limits.h>
#include <rum/task.h>

#define IF 0x200u
#define STACK_PAGES (RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE)
#define BOOT 0u
#define IDLE 1u
#define TASK_SLOTS RUM_TASK_CAPACITY
#define EMERGENCY_STACK_SLOT (RUM_KERNEL_STACK_SLOTS - 1u)

struct task {
    task_id id;
    rum_pid_t process_id;
    task_id parent;
    enum task_kind kind;
    enum task_state state;
    uint32_t stack, stack_slot, stack_base, stack_top;
    void (*entry)(void *);
    void *argument;
    struct paging_space *space;
    bool owns_space;
    struct exception_user_frame user_frame;
    enum task_termination termination;
    rum_result_t exit_status;
    struct task_fault fault;
    struct task_resources resources;
    struct task_event *waiting;
};
static struct task tasks[TASK_SLOTS];
static struct task *current;
static task_id next_id = 3;
static rum_pid_t next_process_id = 1;
static bool ready;
static uint32_t created, exited, reaped, switches;
static struct task_event work_event;
/* Debug symbols also let QEMU checks audit the actual stack owners. */
volatile uint32_t task_idle_stack_base, task_emergency_stack_base, task_current_stack_top;
extern const char __boot_stack_bottom[], __boot_stack_top[];
static _Noreturn void start_task(void);
static _Noreturn void idle_loop(void *argument);

static bool new_stack(struct task *task, uint32_t slot)
{
    if (!paging_kernel_stack_allocate(slot)) return false;
    task->stack_slot = slot;
    task->stack_base = RUM_KERNEL_STACK_SLOT_BASE(slot);
    task->stack_top = RUM_KERNEL_STACK_SLOT_TOP(slot);
    task->resources.kernel_stack_pages = STACK_PAGES;
    return true;
}

static void prepare_stack(struct task *task)
{
    struct kernel_context *context = (void *)(uintptr_t)(task->stack_top - sizeof *context);
    *context = (struct kernel_context){ .eip = (uintptr_t)start_task, .return_address = (uintptr_t)cpu_halt };
    task->stack = (uintptr_t)context;
}

bool task_initialize(void)
{
    if (ready || irq_in_handler() || !paging_kernel_space() ||
        paging_active_space() != paging_kernel_space()) return false;
    struct task idle = { .id = 2, .state = TASK_RUNNABLE,
        .entry = idle_loop, .space = paging_kernel_space() };
    if (!new_stack(&idle, 0)) return false;
    if (!paging_kernel_stack_allocate(EMERGENCY_STACK_SLOT)) {
        (void)paging_kernel_stack_release(0);
        return false;
    }
    uint32_t emergency_base = RUM_KERNEL_STACK_SLOT_BASE(EMERGENCY_STACK_SLOT);
    if (!gdt_set_double_fault_stack(RUM_KERNEL_STACK_SLOT_TOP(EMERGENCY_STACK_SLOT),
                                    paging_directory_address(paging_kernel_space()))) {
        (void)paging_kernel_stack_release(EMERGENCY_STACK_SLOT);
        (void)paging_kernel_stack_release(0);
        return false;
    }
    uint32_t saved = cpu_interrupt_save();
    tasks[BOOT] = (struct task){ .id = 1, .state = TASK_RUNNING,
        .stack_base = (uintptr_t)__boot_stack_bottom, .stack_top = (uintptr_t)__boot_stack_top,
        .space = paging_kernel_space() };
    tasks[IDLE] = idle;
    prepare_stack(&tasks[IDLE]);
    current = &tasks[BOOT];
    if (!gdt_set_kernel_context(current->stack_top,
                                paging_directory_address(current->space))) {
        current = NULL;
        memset(tasks, 0, sizeof tasks);
        cpu_interrupt_restore(saved);
        (void)paging_kernel_stack_release(EMERGENCY_STACK_SLOT);
        (void)paging_kernel_stack_release(0);
        return false;
    }
    task_idle_stack_base = idle.stack_base;
    task_emergency_stack_base = emergency_base;
    task_current_stack_top = current->stack_top;
    ready = true;
    cpu_interrupt_restore(saved);
    return true;
}

/* Call with interrupts disabled. A private space may have exactly one live
   owner, regardless of whether that owner is a kernel task or user process. */
static struct task *available_slot(struct paging_space *space)
{
    struct task *slot = NULL;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        if (space && tasks[i].state != TASK_UNUSED && tasks[i].space == space) return NULL;
        if (!slot && tasks[i].state == TASK_UNUSED) slot = &tasks[i];
    }
    return slot;
}

static bool valid_user_frame(const struct paging_space *space,
                             const struct exception_user_frame *frame)
{
    if (!space || !frame || frame->core.gs != USER_DATA_SELECTOR ||
        frame->core.fs != USER_DATA_SELECTOR || frame->core.es != USER_DATA_SELECTOR ||
        frame->core.ds != USER_DATA_SELECTOR || frame->core.cs != USER_CODE_SELECTOR ||
        frame->ss != USER_DATA_SELECTOR || frame->core.vector || frame->core.error ||
        frame->core.eflags != IF + 2 || frame->esp % RUM_STACK_ALIGNMENT ||
        !memory_range_contains(RUM_USER_BASE, RUM_USER_PROGRAM_END, frame->core.eip, 1) ||
        !memory_range_contains(RUM_USER_STACK_BASE, RUM_USER_STACK_TOP, frame->esp,
                               RUM_ABI_STACK_FIXED_WORDS * sizeof(uint32_t))) return false;
    return paging_user_accessible(space, frame->core.eip, 1, false) &&
           paging_user_accessible(space, frame->esp,
                                  RUM_ABI_STACK_FIXED_WORDS * sizeof(uint32_t), true);
}

task_id task_create(void (*entry)(void *), void *argument, struct paging_space *owned_space)
{
    if (!ready || !entry || irq_in_handler()) return 0;
    uint32_t saved = cpu_interrupt_save();
    struct paging_space_statistics space = {0};
    bool valid = (saved & IF) && next_id &&
        (!owned_space || (paging_space_stats(owned_space, &space) && !space.kernel &&
                          owned_space != paging_active_space()));
    struct task *slot = valid ? available_slot(owned_space) : NULL;
    cpu_interrupt_restore(saved);
    if (!slot) return 0;
    /* No other foreground task can run during construction. IRQs only wake
       published tasks; zeroing the stack does not hold the scheduler lock. */
    uint32_t stack_slot = (uint32_t)(slot - tasks) - 1;
    struct task constructed = { .id = next_id, .kind = TASK_KERNEL, .state = TASK_RUNNABLE,
        .entry = entry, .argument = argument,
        .space = owned_space ? owned_space : paging_kernel_space(), .owns_space = owned_space != NULL };
    if (owned_space) constructed.resources = (struct task_resources){
        .directory_pages = 1, .user_table_pages = space.private_table_pages,
        .user_pages = space.user_pages,
    };
    if (!new_stack(&constructed, stack_slot)) return 0;
    saved = cpu_interrupt_save();
    ++next_id;
    *slot = constructed;
    prepare_stack(slot);
    task_id id = slot->id;
    ++created;
    cpu_interrupt_restore(saved);
    return id;
}

task_id task_create_process(const struct task_process *process)
{
    if (!ready || !process || irq_in_handler()) return 0;
    uint32_t saved = cpu_interrupt_save();
    struct paging_space_statistics space = {0};
    bool valid = (saved & IF) && next_id && next_process_id <= RUM_ABI_PID_MAX &&
        paging_space_stats(process->space, &space) && !space.kernel &&
        process->space != paging_active_space() && valid_user_frame(process->space, &process->user_frame);
    struct task *slot = valid ? available_slot(process->space) : NULL;
    task_id parent = valid ? current->id : 0;
    cpu_interrupt_restore(saved);
    if (!slot) return 0;

    uint32_t stack_slot = (uint32_t)(slot - tasks) - 1;
    struct task constructed = {
        .id = next_id, .process_id = next_process_id, .parent = parent,
        .kind = TASK_PROCESS, .state = TASK_RUNNABLE,
        .space = process->space, .owns_space = true, .user_frame = process->user_frame,
        .resources = { .directory_pages = 1, .user_table_pages = space.private_table_pages,
                       .user_pages = space.user_pages },
    };
    if (!new_stack(&constructed, stack_slot)) return 0;

    /* The record becomes visible only after every owned resource and the saved
       kernel context are complete. Failed calls leave the caller's space alone. */
    saved = cpu_interrupt_save();
    ++next_id;
    ++next_process_id;
    *slot = constructed;
    prepare_stack(slot);
    task_id id = slot->id;
    ++created;
    cpu_interrupt_restore(saved);
    return id;
}

task_id task_current_id(void)
{
    return ready ? current->id : 0;
}

bool task_current_is_process(void)
{
    return ready && current && current->kind == TASK_PROCESS;
}

rum_pid_t task_current_process_id(void)
{
    return task_current_is_process() ? current->process_id : 0;
}

struct paging_space *task_current_process_space(void)
{
    return task_current_is_process() ? current->space : NULL;
}

static struct task_information information(const struct task *task)
{
    return (struct task_information){ .id = task->id, .process_id = task->process_id,
        .parent = task->parent, .kind = task->kind, .state = task->state,
        .stack_slot = task->stack_slot, .stack_base = task->stack_base,
        .stack_top = task->stack_top, .saved_stack = task->stack,
        .user_frame = task->user_frame, .termination = task->termination,
        .exit_status = task->exit_status, .fault = task->fault,
        .resources = task->resources,
        .directory = paging_directory_address(task->space), .owns_space = task->owns_space,
        .owns_stack = task != &tasks[BOOT] };
}

bool task_query(task_id id, struct task_information *result)
{
    if (!ready || !id || !result) return false;
    uint32_t saved = cpu_interrupt_save();
    bool found = false;
    for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
        const struct task *task = &tasks[i];
        if (task->state != TASK_UNUSED && task->id == id) {
            *result = information(task);
            found = true;
            break;
        }
    }
    cpu_interrupt_restore(saved);
    return found;
}

bool task_snapshot_read(struct task_snapshot *snapshot)
{
    if (!snapshot) return false;
    uint32_t saved = cpu_interrupt_save();
    *snapshot = (struct task_snapshot){0};
    if (ready) {
        snapshot->current = current->id;
        snapshot->emergency_stack_pages = STACK_PAGES;
        snapshot->created = created; snapshot->exited = exited;
        snapshot->reaped = reaped; snapshot->switches = switches;
        for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
            if (tasks[i].state == TASK_UNUSED) continue;
            struct task_information item = information(&tasks[i]);
            snapshot->tasks[snapshot->count++] = item;
            ++snapshot->states[item.state];
            if (item.kind == TASK_PROCESS) ++snapshot->processes;
            snapshot->stack_pages += item.resources.kernel_stack_pages;
            snapshot->directory_pages += item.resources.directory_pages;
            snapshot->user_table_pages += item.resources.user_table_pages;
            snapshot->user_pages += item.resources.user_pages;
        }
    }
    cpu_interrupt_restore(saved);
    return ready;
}

static uint32_t reap_exited(bool include_processes)
{
    if (!ready || irq_in_handler()) return 0;
    uint32_t count = 0;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        struct task *task = &tasks[i];
        if (task->state != TASK_EXITED || task == current ||
            (task->kind == TASK_PROCESS && !include_processes) ||
            (task->owns_space && task->space == paging_active_space())) continue;
        /* Kernel-space borrowers share the active directory; only owned
           private directories must be inactive before release. */
        /* Keep owner records and released frames consistent for IRQ snapshots.
           Reclamation never waits for a device or switches execution. */
        uint32_t saved = cpu_interrupt_save();
        if (task->owns_space && !paging_space_destroy(task->space)) {
            cpu_interrupt_restore(saved);
            continue;
        }
        if (!paging_kernel_stack_release(task->stack_slot)) cpu_halt();
        *task = (struct task){0};
        ++reaped;
        cpu_interrupt_restore(saved);
        ++count;
    }
    return count;
}

uint32_t task_reap(void)
{
    return reap_exited(true);
}

/* Call with IF clear. All CR3s borrow supervisor kernel code/data/stacks, so
   the old and new stacks stay mapped during the atomic bookkeeping switch. */
static void schedule(void)
{
    struct task *previous = current, *next = NULL;
    uint32_t index = (uint32_t)(previous - tasks);
    for (uint32_t offset = 1; offset <= TASK_SLOTS; ++offset) {
        uint32_t candidate = (index + offset) % TASK_SLOTS;
        if (candidate != IDLE && tasks[candidate].state == TASK_RUNNABLE) {
            next = &tasks[candidate];
            break;
        }
    }
    if (!next) next = &tasks[IDLE];
    if (previous->state == TASK_RUNNING) previous->state = TASK_RUNNABLE;
    next->state = TASK_RUNNING;
    if (next == previous) return;
    if (!paging_switch_space(next->space) ||
        !gdt_set_kernel_context(next->stack_top,
                                paging_directory_address(next->space))) cpu_halt();
    current = next;
    task_current_stack_top = next->stack_top;
    ++switches;
    kernel_context_switch(&previous->stack, next->stack);
    /* This continuation now belongs to the resumed task, on its own stack. */
    (void)reap_exited(false);
}

bool task_yield(void)
{
    if (!ready || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    if (!(saved & IF) || current == &tasks[IDLE]) { cpu_interrupt_restore(saved); return false; }
    current->state = TASK_RUNNABLE;
    schedule();
    cpu_interrupt_restore(saved);
    return true;
}

_Noreturn void task_exit_with_status(rum_result_t status)
{
    if (!ready || irq_in_handler() || current == &tasks[BOOT] || current == &tasks[IDLE]) cpu_halt();
    (void)cpu_interrupt_save();
    current->waiting = NULL;
    current->termination = TASK_TERMINATION_EXIT;
    current->exit_status = status;
    current->state = TASK_EXITED;
    ++exited;
    schedule();
    cpu_halt(); /* An exited context can never become runnable again. */
}

_Noreturn void task_exit_from_user_fault(const struct exception_frame *frame,
                                         uint32_t fault_address)
{
    if (!ready || !current || current->kind != TASK_PROCESS || !frame ||
        !exception_frame_from_user(frame) || irq_in_handler()) cpu_halt();
    (void)cpu_interrupt_save();
    current->waiting = NULL;
    current->termination = TASK_TERMINATION_FAULT;
    current->user_frame = *(const struct exception_user_frame *)frame;
    current->fault = (struct task_fault){
        .vector = frame->vector,
        .error = frame->error,
        .address = frame->vector == 14 ? fault_address : 0,
        .instruction = frame->eip,
        .stack = exception_frame_esp(frame),
    };
    current->state = TASK_EXITED;
    ++exited;
    schedule();
    cpu_halt();
}

_Noreturn void task_exit(void)
{
    task_exit_with_status(0);
}

uint32_t task_event_sequence(const struct task_event *event)
{
    return event ? event->sequence : 0;
}

void task_event_signal(struct task_event *event)
{
    if (!event) return;
    uint32_t saved = cpu_interrupt_save();
    ++event->sequence;
    if (ready) {
        for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
            if (tasks[i].state == TASK_BLOCKED && tasks[i].waiting == event) {
                tasks[i].waiting = NULL;
                tasks[i].state = TASK_RUNNABLE;
            }
        }
    }
    cpu_interrupt_restore(saved);
}

bool task_wait(struct task_event *event, uint32_t observed)
{
    if (!ready || !event || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    if (!(saved & IF) || current == &tasks[IDLE]) { cpu_interrupt_restore(saved); return false; }
    if (event->sequence == observed) {
        current->waiting = event;
        current->state = TASK_BLOCKED;
        schedule();
    }
    cpu_interrupt_restore(saved);
    return true;
}

struct task_event *task_work_event(void)
{
    return &work_event;
}

static _Noreturn void start_task(void)
{
    (void)reap_exited(false);
    if (current->kind == TASK_PROCESS)
        interrupt_enter(&current->user_frame);
    cpu_interrupt_enable();
    current->entry(current->argument);
    task_exit();
}

static _Noreturn void idle_loop(void *argument)
{
    (void)argument;
    for (;;) {
        (void)reap_exited(false);
        uint32_t saved = cpu_interrupt_save();
        bool pending = false;
        for (uint32_t i = 0; i < TASK_SLOTS; ++i)
            if (i != IDLE && tasks[i].state == TASK_RUNNABLE) { pending = true; break; }
        if (pending) schedule();
        else cpu_idle(); /* Check and STI/HLT are indivisible with respect to IRQs. */
        cpu_interrupt_restore(saved);
    }
}
