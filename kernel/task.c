#include <rum/context.h>
#include <rum/cpu.h>
#include <rum/gdt.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/process_limits.h>
#include <rum/task.h>

#define IF 0x200u
#define STACK_PAGES (RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE)
#define BOOT 0u
#define IDLE 1u
#define TASK_SLOTS RUM_TASK_CAPACITY

struct task {
    task_id id;
    enum task_state state;
    uint32_t stack, stack_slot, stack_base, stack_top;
    void (*entry)(void *);
    void *argument;
    struct paging_space *space;
    bool owns_space;
    struct task_event *waiting;
};
static struct task tasks[TASK_SLOTS];
static struct task *current;
static task_id next_id = 3;
static bool ready;
static uint32_t created, exited, reaped, switches;
static struct task_event work_event;
/* Debug symbols also let QEMU checks audit the actual stack owners. */
volatile uint32_t task_idle_stack_base, task_current_stack_top;
extern const char __boot_stack_bottom[], __boot_stack_top[];
static _Noreturn void start_task(void);
static _Noreturn void idle_loop(void *argument);

static bool new_stack(struct task *task, uint32_t slot)
{
    if (!paging_kernel_stack_allocate(slot)) return false;
    task->stack_slot = slot;
    task->stack_base = RUM_KERNEL_STACK_SLOT_BASE(slot);
    task->stack_top = RUM_KERNEL_STACK_SLOT_TOP(slot);
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
    uint32_t saved = cpu_interrupt_save();
    tasks[BOOT] = (struct task){ .id = 1, .state = TASK_RUNNING,
        .stack_base = (uintptr_t)__boot_stack_bottom, .stack_top = (uintptr_t)__boot_stack_top,
        .space = paging_kernel_space() };
    tasks[IDLE] = idle;
    prepare_stack(&tasks[IDLE]);
    current = &tasks[BOOT];
    if (!gdt_set_kernel_stack(current->stack_top)) {
        current = NULL;
        memset(tasks, 0, sizeof tasks);
        cpu_interrupt_restore(saved);
        (void)paging_kernel_stack_release(0);
        return false;
    }
    task_idle_stack_base = idle.stack_base;
    task_current_stack_top = current->stack_top;
    ready = true;
    cpu_interrupt_restore(saved);
    return true;
}

task_id task_create(void (*entry)(void *), void *argument, struct paging_space *owned_space)
{
    if (!ready || !entry || irq_in_handler()) return 0;
    uint32_t saved = cpu_interrupt_save();
    struct task *slot = NULL;
    bool valid = (saved & IF) && next_id && (!owned_space ||
        (owned_space != paging_kernel_space() && owned_space != paging_active_space() &&
         paging_directory_address(owned_space)));
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        if (owned_space && tasks[i].state != TASK_UNUSED && tasks[i].space == owned_space) valid = false;
        if (!slot && tasks[i].state == TASK_UNUSED) slot = &tasks[i];
    }
    cpu_interrupt_restore(saved);
    if (!valid || !slot) return 0;
    /* No other foreground task can run during construction. IRQs only wake
       published tasks; zeroing the stack does not hold the scheduler lock. */
    uint32_t stack_slot = (uint32_t)(slot - tasks) - 1;
    struct task constructed = { .id = next_id, .state = TASK_RUNNABLE,
        .entry = entry, .argument = argument,
        .space = owned_space ? owned_space : paging_kernel_space(), .owns_space = owned_space != NULL };
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

task_id task_current_id(void)
{
    return ready ? current->id : 0;
}

static struct task_information information(const struct task *task)
{
    return (struct task_information){ .id = task->id, .state = task->state,
        .stack_slot = task->stack_slot, .stack_base = task->stack_base,
        .stack_top = task->stack_top, .saved_stack = task->stack,
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
        snapshot->created = created; snapshot->exited = exited;
        snapshot->reaped = reaped; snapshot->switches = switches;
        for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
            if (tasks[i].state == TASK_UNUSED) continue;
            struct task_information item = information(&tasks[i]);
            snapshot->tasks[snapshot->count++] = item;
            ++snapshot->states[item.state];
            if (item.owns_stack) snapshot->stack_pages += STACK_PAGES;
            if (item.owns_space && item.directory) ++snapshot->directory_pages;
        }
    }
    cpu_interrupt_restore(saved);
    return ready;
}

uint32_t task_reap(void)
{
    if (!ready || irq_in_handler()) return 0;
    uint32_t count = 0;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        struct task *task = &tasks[i];
        if (task->state != TASK_EXITED || task == current ||
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
    if (!paging_switch_space(next->space) || !gdt_set_kernel_stack(next->stack_top)) cpu_halt();
    current = next;
    task_current_stack_top = next->stack_top;
    ++switches;
    kernel_context_switch(&previous->stack, next->stack);
    /* This continuation now belongs to the resumed task, on its own stack. */
    (void)task_reap();
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

_Noreturn void task_exit(void)
{
    if (!ready || irq_in_handler() || current == &tasks[BOOT] || current == &tasks[IDLE]) cpu_halt();
    (void)cpu_interrupt_save();
    current->waiting = NULL;
    current->state = TASK_EXITED;
    ++exited;
    schedule();
    cpu_halt(); /* An exited context can never become runnable again. */
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
    (void)task_reap();
    cpu_interrupt_enable();
    current->entry(current->argument);
    task_exit();
}

static _Noreturn void idle_loop(void *argument)
{
    (void)argument;
    for (;;) {
        (void)task_reap();
        uint32_t saved = cpu_interrupt_save();
        bool pending = false;
        for (uint32_t i = 0; i < TASK_SLOTS; ++i)
            if (i != IDLE && tasks[i].state == TASK_RUNNABLE) { pending = true; break; }
        if (pending) schedule();
        else cpu_idle(); /* Check and STI/HLT are indivisible with respect to IRQs. */
        cpu_interrupt_restore(saved);
    }
}
