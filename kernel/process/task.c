#include <rum/context.h>
#include <rum/block.h>
#include <rum/cpu.h>
#include <rum/fs.h>
#include <rum/gdt.h>
#include <rum/handles.h>
#include <rum/interrupts.h>
#include <rum/abi/syscall.h>
#include <rum/abi/error.h>
#include <rum/memory.h>
#include <rum/process_limits.h>
#include <rum/task.h>
#include <rum/session.h>
#include <rum/keyboard.h>
#include <rum/timer.h>

#define IF 0x200u
#define STACK_PAGES (RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE)
#define BOOT 0u
#define IDLE 1u
#define TASK_SLOTS RUM_TASK_CAPACITY
#define EMERGENCY_STACK_SLOT (RUM_KERNEL_STACK_SLOTS - 1u)

/* A process owns resources; its initial thread owns execution. Kernel workers
   use the same resource container with PID zero. No shared-address-space user
   threads are published yet. */
struct process_state {
    rum_pid_t process_id;
    task_id parent;
    enum task_kind kind;
    struct paging_space *space;
    bool owns_space;
    enum task_termination termination;
    rum_result_t exit_status;
    struct task_fault fault;
    struct task_resources resources; /* Address-space pages only. */
    struct fs_context filesystem;
    struct process_handles handles;
    bool cancellation_requested;
    bool orphaned;
    struct task_event children_event;
    struct task_event console_event;
    char name[RUM_PROCESS_NAME_CAPACITY];
    struct console_member console;
    char command_text[RUM_COMMAND_TEXT_CAPACITY];
};

struct task {
    task_id id;
    enum task_state state;
    uint32_t stack, stack_slot, stack_base, stack_top, stack_pages;
    uint32_t switch_holds;
    uint32_t slice_ticks;
    bool reschedule;
    void (*entry)(void *);
    void *argument;
    struct exception_user_frame user_frame;
    struct task_event *waiting;
    uint32_t deadline;
    bool deadline_active;
    enum task_wait_result wake_reason;
    struct process_state *process;
};
static struct process_state processes[TASK_SLOTS];
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

static void clear_wait(struct task *task)
{
    task->waiting = NULL;
    task->deadline_active = false;
    task->deadline = 0;
}

/* IF clear; no allocation, destruction or context switch. */
static void wake_task(struct task *task, enum task_wait_result reason)
{
    clear_wait(task);
    task->wake_reason = reason;
    task->state = TASK_RUNNABLE;
}

/* Caller publishes only with interrupts disabled, after construction completes. */
static void publish(struct task *slot, const struct task *prepared)
{
    if (cpu_interrupt_save() & IF) cpu_halt();
    processes[slot - tasks] = *prepared->process;
    *slot = *prepared;
    slot->process = &processes[slot - tasks];
}

static bool new_stack(struct task *task, uint32_t slot)
{
    if (!paging_kernel_stack_allocate(slot)) return false;
    task->stack_slot = slot;
    task->stack_base = RUM_KERNEL_STACK_SLOT_BASE(slot);
    task->stack_top = RUM_KERNEL_STACK_SLOT_TOP(slot);
    task->stack_pages = STACK_PAGES;
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
    struct process_state idle_process = { .space = paging_kernel_space() };
    struct task idle = { .id = 2, .state = TASK_RUNNABLE,
        .entry = idle_loop, .process = &idle_process };
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
    struct fs_context filesystem = {0};
    if (!fs_initialize() || fs_context_initialize(&filesystem) != FS_OK) {
        (void)paging_kernel_stack_release(EMERGENCY_STACK_SLOT);
        (void)paging_kernel_stack_release(0);
        return false;
    }
    uint32_t saved = cpu_interrupt_save();
    processes[BOOT] = (struct process_state){
        .space = paging_kernel_space(), .filesystem = filesystem };
    tasks[BOOT] = (struct task){ .id = 1, .state = TASK_RUNNING,
        .stack_base = (uintptr_t)__boot_stack_bottom, .stack_top = (uintptr_t)__boot_stack_top,
        .process = &processes[BOOT] };
    publish(&tasks[IDLE], &idle);
    prepare_stack(&tasks[IDLE]);
    current = &tasks[BOOT];
    if (!gdt_set_kernel_context(current->stack_top,
                                paging_directory_address(current->process->space))) {
        current = NULL;
        memset(tasks, 0, sizeof tasks);
        memset(processes, 0, sizeof processes);
        cpu_interrupt_restore(saved);
        (void)paging_kernel_stack_release(EMERGENCY_STACK_SLOT);
        (void)paging_kernel_stack_release(0);
        (void)fs_context_destroy(&filesystem);
        return false;
    }
    task_idle_stack_base = idle.stack_base;
    task_emergency_stack_base = emergency_base;
    task_current_stack_top = current->stack_top;
    ready = true;
    current->slice_ticks = TASK_TIME_SLICE_TICKS;
    cpu_interrupt_restore(saved);
    return true;
}

/* Call with interrupts disabled. A private space may have exactly one live
   owner, regardless of whether that owner is a kernel task or user process. */
static struct task *available_slot(struct paging_space *space)
{
    struct task *slot = NULL;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        if (space && tasks[i].state != TASK_UNUSED && tasks[i].process->space == space) return NULL;
        if (!slot && tasks[i].state == TASK_UNUSED) slot = &tasks[i];
    }
    return slot;
}

/* Keep one registry slot for a foreground control tool when asynchronous
   children fill the pool. Kernel producers retain their existing policy. */
static bool background_capacity(void)
{
    if (current->process->kind != TASK_PROCESS) return true;
    unsigned free = 0;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) free += tasks[i].state == TASK_UNUSED;
    return free > 1;
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
    if (!slot || !task_switch_hold()) return 0;
    /* No other foreground task can run during construction. IRQs only wake
       published tasks; zeroing the stack does not hold the scheduler lock. */
    uint32_t stack_slot = (uint32_t)(slot - tasks) - 1;
    struct process_state owner = { .kind = TASK_KERNEL,
        .space = owned_space ? owned_space : paging_kernel_space(), .owns_space = owned_space != NULL };
    struct task constructed = { .id = next_id, .state = TASK_RUNNABLE,
        .entry = entry, .argument = argument, .process = &owner };
    if (owned_space) owner.resources = (struct task_resources){
        .directory_pages = 1, .user_table_pages = space.private_table_pages,
        .user_pages = space.user_pages,
    };
    if (fs_context_clone(&current->process->filesystem, &owner.filesystem) != FS_OK) {
        (void)task_switch_release();
        return 0;
    }
    if (!new_stack(&constructed, stack_slot)) {
        (void)fs_context_destroy(&owner.filesystem);
        (void)task_switch_release();
        return 0;
    }
    saved = cpu_interrupt_save();
    ++next_id;
    prepare_stack(&constructed);
    publish(slot, &constructed);
    task_id id = slot->id;
    ++created;
    cpu_interrupt_restore(saved);
    (void)task_switch_release();
    return id;
}

static task_id create_process(const struct task_process *process, bool make_foreground, bool shell)
{
    if (!ready || !process || irq_in_handler()) return 0;
    uint32_t saved = cpu_interrupt_save();
    struct paging_space_statistics space = {0};
    bool valid = (saved & IF) && next_id && next_process_id <= RUM_ABI_PID_MAX &&
        (make_foreground || background_capacity()) &&
        console_can_launch(current->process->kind == TASK_PROCESS ? &current->process->console : NULL, make_foreground) &&
        paging_space_stats(process->space, &space) && !space.kernel &&
        process->space != paging_active_space() && valid_user_frame(process->space, &process->user_frame);
    struct task *slot = valid ? available_slot(process->space) : NULL;
    task_id parent = valid ? current->id : 0;
    cpu_interrupt_restore(saved);
    if (!slot || !task_switch_hold()) return 0;

    uint32_t stack_slot = (uint32_t)(slot - tasks) - 1;
    struct process_state owner = { .process_id = next_process_id, .parent = parent,
        .kind = TASK_PROCESS, .space = process->space, .owns_space = true,
        .resources = { .directory_pages = 1, .user_table_pages = space.private_table_pages,
                       .user_pages = space.user_pages } };
    struct task constructed = { .id = next_id, .state = TASK_RUNNABLE,
        .process = &owner, .user_frame = process->user_frame };
    const char *name = process->name[0] ? process->name : "program";
    for (unsigned i = 0; i < sizeof owner.name - 1 && name[i]; ++i)
        owner.name[i] = (unsigned char)name[i] >= 32 && (unsigned char)name[i] <= 126 ? name[i] : '?';
    if (fs_context_clone(&current->process->filesystem, &owner.filesystem) != FS_OK) {
        (void)task_switch_release();
        return 0;
    }
    if (!new_stack(&constructed, stack_slot)) {
        (void)fs_context_destroy(&owner.filesystem);
        (void)task_switch_release();
        return 0;
    }

    /* The record becomes visible only after every owned resource and the saved
       kernel context are complete. Failed calls leave the caller's space alone. */
    saved = cpu_interrupt_save();
    struct console_member *parent_console = current->process->kind == TASK_PROCESS ? &current->process->console : NULL;
    /* Ctrl+C can arrive during stack/cwd construction. Recheck under the
       publication lock so cancellation never leaves a half-attached session. */
    if (!console_can_launch(parent_console, make_foreground) || task_current_cancelled()) {
        cpu_interrupt_restore(saved);
        (void)paging_kernel_stack_release(stack_slot);
        (void)fs_context_destroy(&owner.filesystem);
        (void)task_switch_release();
        return 0;
    }
    process_handles_initialize(&owner.handles);
    ++next_id;
    ++next_process_id;
    prepare_stack(&constructed);
    publish(slot, &constructed);
    task_id id = slot->id;
    if (!console_attach(&slot->process->console, id, parent_console, make_foreground)) cpu_halt();
    if (shell && !console_mark_shell(&slot->process->console)) cpu_halt();
    ++created;
    cpu_interrupt_restore(saved);
    (void)task_switch_release();
    return id;
}

rum_result_t task_process_creation_status(bool make_foreground)
{
    if (!ready || irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    rum_result_t result = !(saved & IF) || current == &tasks[IDLE] ? -RUM_EBUSY :
        !next_id || next_process_id > RUM_ABI_PID_MAX ? -RUM_EOVERFLOW :
        !console_can_launch(current->process->kind == TASK_PROCESS ? &current->process->console : NULL, make_foreground) ? -RUM_EBUSY :
        !available_slot(NULL) || (!make_foreground && !background_capacity()) ? -RUM_ENOMEM : 0;
    cpu_interrupt_restore(saved);
    return result;
}

task_id task_create_process(const struct task_process *process)
{
    return create_process(process, false, false);
}

task_id task_create_foreground_process(const struct task_process *process)
{
    return create_process(process, true, false);
}

task_id task_create_shell_process(const struct task_process *process)
{
    if (!ready || irq_in_handler() || current->process->kind != TASK_KERNEL) return 0;
    return create_process(process, true, true);
}

bool task_mark_foreground_shell(task_id id)
{
    if (!ready || !id || irq_in_handler() || current->process->kind != TASK_KERNEL) return false;
    uint32_t saved = cpu_interrupt_save();
    bool marked = false;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].id == id &&
        tasks[i].process->parent == current->id && tasks[i].process->kind == TASK_PROCESS &&
        tasks[i].state == TASK_RUNNABLE) {
        marked = console_mark_shell(&tasks[i].process->console); break;
    }
    cpu_interrupt_restore(saved);
    return marked;
}

bool task_current_is_shell(void)
{
    return task_current_is_process() && console_is_shell(&current->process->console);
}

bool task_mark_shell_command(task_id id, const char *text)
{
    if (!task_current_is_shell() || !id || !text || irq_in_handler()) return false;
    size_t bytes = 0;
    while (bytes < RUM_COMMAND_TEXT_CAPACITY && text[bytes]) ++bytes;
    if (bytes == RUM_COMMAND_TEXT_CAPACITY) return false;
    uint32_t saved = cpu_interrupt_save();
    bool marked = false;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].id == id &&
        tasks[i].process->parent == current->id && tasks[i].process->kind == TASK_PROCESS &&
        tasks[i].state == TASK_RUNNABLE) {
        if (!console_grant_command(&current->process->console, &tasks[i].process->console)) break;
        memcpy(tasks[i].process->command_text, text, bytes + 1);
        marked = true; break;
    }
    cpu_interrupt_restore(saved);
    return marked;
}

const char *task_current_command_text(void)
{
    return task_current_is_process() && console_command_shell(&current->process->console) ? current->process->command_text : NULL;
}

rum_result_t task_session_control(unsigned action, const char *path)
{
    if (action < RUM_SESSION_CHDIR || action > RUM_SESSION_RECOVERY) return -RUM_EINVAL;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    struct console_member *shell = console_command_shell(&current->process->console);
    if (!shell) return -RUM_EACCES;
    struct task *parent = NULL;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].state != TASK_UNUSED &&
        &tasks[i].process->console == shell && tasks[i].id == current->process->parent) parent = &tasks[i];
    if (!parent || !console_is_shell(&parent->process->console) || parent->state != TASK_BLOCKED ||
        parent->waiting != &parent->process->children_event) return -RUM_EACCES;
    /* The parent cannot run or be reaped until this direct child ends. Disk
       traversal runs with interrupts enabled, outside any registry update. */
    if (action == RUM_SESSION_CHDIR) return process_fs_error(fs_context_chdir(&parent->process->filesystem, path));
    return console_request_exit(&current->process->console,
        action == RUM_SESSION_RECOVERY ? RUM_SHELL_RECOVERY_STATUS : 0) ? 0 : -RUM_EACCES;
}

void task_exit_shell_if_requested(void)
{
    int32_t status;
    if (task_current_is_shell() && console_exit_requested(&current->process->console, &status))
        task_exit_with_status(status);
}

task_id task_current_id(void)
{
    return ready ? current->id : 0;
}

bool task_current_is_process(void)
{
    return ready && current && current->process->kind == TASK_PROCESS;
}

rum_pid_t task_current_process_id(void)
{
    return task_current_is_process() ? current->process->process_id : 0;
}

struct paging_space *task_current_process_space(void)
{
    return task_current_is_process() ? current->process->space : NULL;
}

struct fs_context *task_current_filesystem(void)
{
    return ready && current && current->process->filesystem.directory && !irq_in_handler() ? &current->process->filesystem : NULL;
}

struct process_handles *task_current_handles(void)
{
    return task_current_is_process() && !irq_in_handler() ? &current->process->handles : NULL;
}

unsigned task_handle_count(task_id id)
{
    uint32_t saved = cpu_interrupt_save();
    unsigned count = 0;
    if (ready && id) for (unsigned i = 0; i < TASK_SLOTS; ++i)
        if (tasks[i].state != TASK_UNUSED && tasks[i].id == id) {
            count = process_handles_count(&tasks[i].process->handles); break;
        }
    cpu_interrupt_restore(saved);
    return count;
}

bool task_working_directory(task_id id, char *buffer, size_t capacity)
{
    if (!ready || !id || !buffer || !capacity) return false;
    uint32_t saved = cpu_interrupt_save();
    bool found = false;
    buffer[0] = 0;
    for (unsigned i = 0; i < TASK_SLOTS; ++i) {
        const struct task *task = &tasks[i];
        if (task->state == TASK_UNUSED || task->id != id || !task->process->filesystem.directory) continue;
        size_t length = 0;
        while (length < FS_PATH_CAPACITY && task->process->filesystem.path[length]) ++length;
        if (length < capacity && length < FS_PATH_CAPACITY) {
            memcpy(buffer, task->process->filesystem.path, length + 1); found = true;
        }
        break;
    }
    cpu_interrupt_restore(saved);
    return found;
}

static struct task_information information(const struct task *task)
{
    return (struct task_information){ .id = task->id, .process_id = task->process->process_id,
        .parent = task->process->parent, .kind = task->process->kind, .state = task->state,
        .stack_slot = task->stack_slot, .stack_base = task->stack_base,
        .stack_top = task->stack_top, .saved_stack = task->stack,
        .user_frame = task->user_frame, .termination = task->process->termination,
        .exit_status = task->process->exit_status, .fault = task->process->fault,
        .resources = { .kernel_stack_pages = task->stack_pages,
            .directory_pages = task->process->resources.directory_pages,
            .user_table_pages = task->process->resources.user_table_pages,
            .user_pages = task->process->resources.user_pages },
        .cancellation_requested = task->process->cancellation_requested,
        .directory = paging_directory_address(task->process->space), .owns_space = task->process->owns_space,
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

uint32_t task_process_list(struct rum_process_entry *entries, uint32_t capacity, uint32_t *total)
{
    if (!total || capacity > RUM_PROCESS_LIST_LIMIT || (capacity && !entries)) return 0;
    uint32_t saved = cpu_interrupt_save(), count = 0;
    *total = 0;
    if (ready) for (unsigned i = 2; i < TASK_SLOTS; ++i) {
        const struct task *task = &tasks[i];
        if (task->state == TASK_UNUSED || task->process->kind != TASK_PROCESS) continue;
        ++*total;
        if (count == capacity) continue;
        struct rum_process_entry *entry = &entries[count++];
        *entry = (struct rum_process_entry){ .process_id = task->process->process_id,
            .state = task->state == TASK_RUNNABLE ? RUM_PROCESS_RUNNABLE : task->state == TASK_RUNNING ? RUM_PROCESS_RUNNING :
                task->state == TASK_BLOCKED ? RUM_PROCESS_WAITING : RUM_PROCESS_ENDED,
            .flags = task->process->cancellation_requested ? RUM_PROCESS_CANCELLING : 0 };
        for (unsigned j = 0; j < TASK_SLOTS; ++j)
            if (tasks[j].state != TASK_UNUSED && tasks[j].id == task->process->parent)
                entry->parent_id = tasks[j].process->process_id;
        memcpy(entry->name, task->process->name, sizeof entry->name);
    }
    cpu_interrupt_restore(saved); return count;
}

struct task_event *task_shell_input_event(void)
{
    return task_current_is_shell() ? &current->process->console_event : NULL;
}
void task_notify_shell_input(void)
{
    if (!ready) return;
    uint32_t saved = cpu_interrupt_save();
    for (unsigned i = 2; i < TASK_SLOTS; ++i)
        if (tasks[i].state != TASK_UNUSED && console_is_shell(&tasks[i].process->console) &&
            console_owns_input(&tasks[i].process->console)) task_event_signal(&tasks[i].process->console_event);
    cpu_interrupt_restore(saved);
}
bool task_shell_has_completion(void)
{
    if (!task_current_is_shell()) return false;
    uint32_t saved = cpu_interrupt_save(); bool complete = false;
    for (unsigned i = 2; i < TASK_SLOTS; ++i)
        if (tasks[i].state == TASK_EXITED && tasks[i].process->kind == TASK_PROCESS &&
            tasks[i].process->parent == current->id && !tasks[i].process->orphaned) complete = true;
    cpu_interrupt_restore(saved); return complete;
}

bool task_snapshot_read(struct task_snapshot *snapshot)
{
    if (!snapshot) return false;
    uint32_t saved = cpu_interrupt_save();
    *snapshot = (struct task_snapshot){0};
    if (ready) {
        snapshot->current = current->id;
        snapshot->console = console_stats();
        snapshot->emergency_stack_pages = STACK_PAGES;
        snapshot->created = created; snapshot->exited = exited;
        snapshot->reaped = reaped; snapshot->switches = switches;
        for (uint32_t i = 0; i < TASK_SLOTS; ++i) {
            if (tasks[i].state == TASK_UNUSED) continue;
            struct task_information item = information(&tasks[i]);
            snapshot->tasks[snapshot->count++] = item;
            ++snapshot->states[item.state];
            if (tasks[i].waiting) ++snapshot->event_waiters;
            if (tasks[i].deadline_active) ++snapshot->deadline_waiters;
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

/* Call with interrupts disabled. */
static bool reap_slot(struct task *task)
{
    if (!task || task->state != TASK_EXITED || task == current ||
        task->waiting || task->deadline_active || task->deadline ||
        task->process->console.references || task->process->console.live ||
        (task->process->owns_space && task->process->space == paging_active_space())) return false;
    /* Exit must release file handles before this interrupt-protected reaper.
       Refuse to discard a record with outstanding ownership. */
    if (process_handles_count(&task->process->handles)) return false;
    /* Pin release is allocation-free and performs no backend I/O. Keep the
       address space intact if a foreground filesystem operation is busy. */
    if (fs_context_destroy(&task->process->filesystem) != FS_OK) return false;
    if (task->process->owns_space && !paging_space_destroy(task->process->space)) return false;
    if (!paging_kernel_stack_release(task->stack_slot)) cpu_halt();
    if (!console_detach(&task->process->console)) cpu_halt();
    *task->process = (struct process_state){0};
    *task = (struct task){0};
    ++reaped;
    return true;
}

static uint32_t reap_exited(bool include_processes)
{
    if (!ready || irq_in_handler()) return 0;
    uint32_t count = 0;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        struct task *task = &tasks[i];
        if (task->state == TASK_UNUSED ||
            (task->process->kind == TASK_PROCESS && !task->process->orphaned &&
             (!include_processes || task->process->parent != current->id))) continue;
        /* Kernel-space borrowers share the active directory; only owned
           private directories must be inactive before release. */
        /* Keep owner records and released frames consistent for IRQ snapshots.
           Reclamation never waits for a device or switches execution. */
        uint32_t saved = cpu_interrupt_save();
        bool released = reap_slot(task);
        cpu_interrupt_restore(saved);
        if (released) ++count;
    }
    return count;
}

bool task_reap_process(task_id id)
{
    if (!ready || !id || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    bool released = false;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        struct task *task = &tasks[i];
        if (task->id == id && task->process->kind == TASK_PROCESS &&
            task->process->parent == current->id) {
            released = reap_slot(task);
            break;
        }
    }
    cpu_interrupt_restore(saved);
    return released;
}

uint32_t task_reap(void)
{
    return reap_exited(true);
}

static bool switch_protected(void)
{
    return current->switch_holds || fs_operation_active() || block_request_active();
}

/* Call with IF clear. All CR3s borrow supervisor kernel code/data/stacks, so
   the old and new stacks stay mapped during the atomic bookkeeping switch. */
static void schedule(void)
{
    if ((cpu_interrupt_save() & IF) || !ready || irq_in_handler() ||
        !current || !current->process || switch_protected() ||
        current->process->space != paging_active_space() ||
        rum_tss.esp0 != current->stack_top) cpu_halt();
    struct task *previous = current, *next = NULL;
    /* Include the running task in the circular search. If it is the only
       runnable context, keep it instead of taking a detour through idle. */
    if (previous->state == TASK_RUNNING) previous->state = TASK_RUNNABLE;
    uint32_t index = (uint32_t)(previous - tasks);
    for (uint32_t offset = 1; offset <= TASK_SLOTS; ++offset) {
        uint32_t candidate = (index + offset) % TASK_SLOTS;
        if (candidate != IDLE && tasks[candidate].state == TASK_RUNNABLE) {
            next = &tasks[candidate];
            break;
        }
    }
    if (!next) next = &tasks[IDLE];
    next->state = TASK_RUNNING;
    previous->reschedule = false;
    next->reschedule = false;
    next->slice_ticks = TASK_TIME_SLICE_TICKS;
    if (next == previous) return;
    if (!paging_switch_space(next->process->space) ||
        !gdt_set_kernel_context(next->stack_top,
                                paging_directory_address(next->process->space))) cpu_halt();
    current = next;
    task_current_stack_top = next->stack_top;
    ++switches;
    kernel_context_switch(&previous->stack, next->stack);
    /* This continuation now belongs to the resumed task, on its own stack. */
    (void)reap_exited(false);
}

bool task_switch_hold(void)
{
    if (!ready || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    bool held = current != &tasks[IDLE] && current->switch_holds != UINT32_MAX;
    if (held) ++current->switch_holds;
    cpu_interrupt_restore(saved);
    return held;
}

bool task_switch_release(void)
{
    if (!ready || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    bool released = current != &tasks[IDLE] && current->switch_holds != 0;
    if (released) --current->switch_holds;
    cpu_interrupt_restore(saved);
    return released;
}

bool task_yield(void)
{
    if (!ready || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    if (!(saved & IF) || current == &tasks[IDLE] || switch_protected()) {
        cpu_interrupt_restore(saved); return false;
    }
    current->state = TASK_RUNNABLE;
    schedule();
    cpu_interrupt_restore(saved);
    return true;
}

static _Noreturn void terminate_current(enum task_termination termination,
                                        rum_result_t status,
                                        const struct exception_frame *frame,
                                        uint32_t fault_address)
{
    if (!ready || irq_in_handler() || current == &tasks[BOOT] || current == &tasks[IDLE] ||
        switch_protected()) cpu_halt();
    (void)cpu_interrupt_save();
    clear_wait(current);
    current->process->termination = termination;
    current->process->exit_status = status;
    if (frame) {
        current->user_frame = *(const struct exception_user_frame *)frame;
        current->process->fault = (struct task_fault){
            .vector = frame->vector,
            .error = frame->error,
            .address = frame->vector == 14 ? fault_address : 0,
            .instruction = frame->eip,
            .stack = exception_frame_esp(frame),
        };
    }
    /* IRQ entry may arrive with IF clear. Copy its frame first, then release
       file pins in task context before publishing EXITED. IRQs cannot schedule
       a kernel continuation; no backend callback runs under the update lock. */
    cpu_interrupt_enable();
    if (process_handles_destroy(&current->process->handles)) cpu_halt();
    (void)cpu_interrupt_save();
    /* Abandon the complete descendant tree before publishing the parent's
       exit. Adopt it into boot's cleanup ownership, wake/cancel live members,
       and let the ordinary inactive-resource reaper discard their results.
       Build the set first: rewriting parents during discovery loses ancestry. */
    bool descendants[TASK_SLOTS] = {0};
    task_id pending[TASK_SLOTS] = { current->id };
    unsigned head = 0, tail = 1;
    while (head < tail) {
        task_id parent = pending[head++];
        for (unsigned i = 2; i < TASK_SLOTS; ++i) {
            if (tasks[i].state == TASK_UNUSED || &tasks[i] == current || descendants[i] ||
                tasks[i].process->parent != parent) continue;
            descendants[i] = true;
            pending[tail++] = tasks[i].id; /* Each fixed slot is queued once. */
        }
    }
    if (console_is_root(&current->process->console))
        for (unsigned i = 2; i < TASK_SLOTS; ++i)
            if (tasks[i].state != TASK_UNUSED && &tasks[i] != current &&
                console_same_session(&tasks[i].process->console, &current->process->console)) descendants[i] = true;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (descendants[i]) {
        struct task *child = &tasks[i];
        child->process->parent = tasks[BOOT].id;
        child->process->orphaned = true;
        child->process->cancellation_requested = true;
        console_cancel_member(&child->process->console);
        if (child->state == TASK_BLOCKED) {
            wake_task(child, TASK_WAIT_CANCELLED);
        }
    }
    if (console_member_exit(&current->process->console)) keyboard_discard_input();
    current->state = TASK_EXITED;
    ++exited;
    if (current->process->kind == TASK_PROCESS)
        for (unsigned i = 0; i < TASK_SLOTS; ++i)
            if (tasks[i].state != TASK_UNUSED && tasks[i].id == current->process->parent) {
                task_event_signal(&tasks[i].process->children_event);
                if (console_is_shell(&tasks[i].process->console)) task_event_signal(&tasks[i].process->console_event);
                break;
            }
    schedule();
    cpu_halt(); /* An exited context can never become runnable again. */
}

_Noreturn void task_exit_with_status(rum_result_t status)
{
    terminate_current(TASK_TERMINATION_EXIT, status, NULL, 0);
}

_Noreturn void task_exit_from_user_fault(const struct exception_frame *frame,
                                         uint32_t fault_address)
{
    if (!ready || !current || current->process->kind != TASK_PROCESS || !frame ||
        !exception_frame_from_user(frame) || irq_in_handler()) cpu_halt();
    terminate_current(TASK_TERMINATION_FAULT, 0, frame, fault_address);
}

_Noreturn void task_exit(void)
{
    task_exit_with_status(0);
}

bool task_foreground_end(task_id id)
{
    if (!ready || !id || irq_in_handler()) return false;
    uint32_t saved = cpu_interrupt_save();
    bool cleared = false;
    for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
        if (tasks[i].id == id && tasks[i].process->kind == TASK_PROCESS &&
            tasks[i].process->parent == current->id && tasks[i].state == TASK_EXITED &&
            tasks[i].process->console.foreground_launch) {
            tasks[i].process->console.foreground_launch = false;
            cleared = true;
            break;
        }
    }
    cpu_interrupt_restore(saved);
    return cleared;
}

bool task_cancel_foreground(void)
{
    uint32_t saved = cpu_interrupt_save();
    bool requested = false;
    task_id foreground = console_cancellation_target();
    if (ready && foreground) {
        for (uint32_t i = 2; i < TASK_SLOTS; ++i) {
            struct task *task = &tasks[i];
            if (task->id != foreground || task->process->kind != TASK_PROCESS ||
                task->state == TASK_EXITED || task->state == TASK_UNUSED) continue;
            task->process->cancellation_requested = true;
            console_cancel_member(&task->process->console);
            if (task->state == TASK_BLOCKED) {
                wake_task(task, TASK_WAIT_CANCELLED);
            }
            requested = true;
            break;
        }
    }
    cpu_interrupt_restore(saved);
    return requested;
}

bool task_current_cancelled(void)
{
    uint32_t saved = cpu_interrupt_save();
    bool requested = ready && current && current->process->kind == TASK_PROCESS &&
                     current->process->cancellation_requested;
    cpu_interrupt_restore(saved);
    return requested;
}

bool task_current_owns_console(void)
{
    return task_current_is_process() && !current->process->orphaned && console_owns_input(&current->process->console);
}

rum_result_t task_request_termination(rum_pid_t id, bool session)
{
    if (!id || id > RUM_ABI_PID_MAX) return -RUM_EINVAL;
    if (!task_current_is_process() || irq_in_handler()) return -RUM_EACCES;
    uint32_t saved = cpu_interrupt_save();
    struct task *target = NULL, *shell = NULL;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].state != TASK_UNUSED) {
        if (tasks[i].process->kind == TASK_PROCESS && tasks[i].process->process_id == id) target = &tasks[i];
        if (tasks[i].id == current->process->parent && console_is_shell(&tasks[i].process->console)) shell = &tasks[i];
    }
    rum_result_t result = 0;
    bool authorized = session ? shell && console_command_shell(&current->process->console) == &shell->process->console &&
        target && console_same_session(&target->process->console, &shell->process->console) && target->process->parent == shell->id :
        target && target->process->parent == current->id;
    if (!target) result = -RUM_ESRCH;
    else if (!authorized || target == current || target->process->console.shell || target->process->orphaned)
        result = -RUM_EACCES;
    else if (target->state != TASK_EXITED) {
        target->process->cancellation_requested = true;
        console_cancel_member(&target->process->console);
        if (target->state == TASK_BLOCKED) {
            wake_task(target, TASK_WAIT_CANCELLED);
        }
    }
    cpu_interrupt_restore(saved);
    return result;
}

void task_cancel_current_if_requested(void)
{
    if (task_current_cancelled())
        terminate_current(TASK_TERMINATION_CANCELLED, 0, NULL, 0);
}

void task_timer_tick(uint32_t now)
{
    if (!ready) return;
    for (unsigned i = 0; i < TASK_SLOTS; ++i)
        if (tasks[i].state == TASK_BLOCKED && tasks[i].deadline_active &&
            timer_deadline_reached(now, tasks[i].deadline)) wake_task(&tasks[i], TASK_WAIT_TIMEOUT);
    /* A saturating per-dispatch budget does not depend on the absolute PIT
       count, so wrapping that clock cannot extend or shorten a time slice. */
    if (!current || !current->slice_ticks) return;
    if (!--current->slice_ticks) current->reschedule = true;
}

void task_on_user_return(const struct exception_frame *frame)
{
    if (!ready || !frame || !exception_frame_from_user(frame)) return;
    /* A real CPL-3 entry uses TSS.ESP0 and has exactly one complete frame at
       its top. Nested IRQs interrupt CPL 0 and cannot use this boundary. No
       user pointer or user-selected frame is accepted here. */
    if ((cpu_interrupt_save() & IF) || irq_in_handler() ||
        current->process->kind != TASK_PROCESS || switch_protected() ||
        (uintptr_t)frame != current->stack_top - sizeof(struct exception_user_frame)) cpu_halt();
    current->user_frame = *(const struct exception_user_frame *)frame;
    task_cancel_current_if_requested();
    if (current->reschedule) schedule();
    task_cancel_current_if_requested(); /* Cancellation may arrive while away. */
    /* The complete interrupt frame is still on this task's guarded stack.
       Resuming schedule returns to assembly, which restores it with IRET. */
}

bool task_wait_process(task_id id, struct task_information *result)
{
    if (!ready || !id || !result || irq_in_handler()) return false;
    struct task_information child;
    return task_query(id, &child) && child.kind == TASK_PROCESS &&
           child.parent == current->id && task_wait_child(child.process_id, false, result) == 1;
}

rum_result_t task_wait_child(rum_pid_t id, bool nohang, struct task_information *result)
{
    if (id > RUM_ABI_PID_MAX || !result) return -RUM_EINVAL;
    if (!ready || irq_in_handler()) return -RUM_EBUSY;
    uint32_t saved = cpu_interrupt_save();
    bool safe = (saved & IF) && current != &tasks[IDLE] && !switch_protected();
    cpu_interrupt_restore(saved);
    if (!safe) return -RUM_EBUSY;
    struct task_event *event = &current->process->children_event;
    for (;;) {
        if (task_current_cancelled()) return -RUM_EINTR;
        uint32_t observed = task_event_sequence(event);
        saved = cpu_interrupt_save();
        bool found = false;
        for (unsigned i = 2; i < TASK_SLOTS; ++i) {
            const struct task *child = &tasks[i];
            if (child->state == TASK_UNUSED || child->process->kind != TASK_PROCESS ||
                child->process->orphaned || child->process->parent != current->id ||
                (id && child->process->process_id != id)) continue;
            found = true;
            if (child->state == TASK_EXITED) {
                *result = information(child);
                cpu_interrupt_restore(saved);
                return 1;
            }
        }
        cpu_interrupt_restore(saved);
        if (!found) return -RUM_ECHILD;
        if (nohang) return 0;
        if (!task_wait(event, observed)) return -RUM_EBUSY;
    }
}

bool task_collect_child(task_id id)
{
    if (!ready || irq_in_handler()) return false;
    struct task_information info;
    if (!task_query(id, &info) || info.parent != current->id || info.state != TASK_EXITED) return false;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].id == id &&
        tasks[i].process->console.foreground_launch && !task_foreground_end(id)) return false;
    return task_reap_process(id);
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
                wake_task(&tasks[i], TASK_WAIT_EVENT);
            }
        }
    }
    cpu_interrupt_restore(saved);
}

static enum task_wait_result wait_common(struct task_event *event, uint32_t observed,
                                         bool timed, uint32_t duration)
{
    if (!ready || (!event && !timed) || irq_in_handler()) return TASK_WAIT_INVALID;
    uint32_t saved = cpu_interrupt_save();
    if (!(saved & IF) || current == &tasks[IDLE] || switch_protected() ||
        (timed && duration >= TIMER_HALF_RANGE)) {
        cpu_interrupt_restore(saved); return TASK_WAIT_INVALID;
    }
    enum task_wait_result reason;
    if (task_current_cancelled()) reason = TASK_WAIT_CANCELLED;
    else if (event && event->sequence != observed) reason = TASK_WAIT_EVENT;
    else if (timed && !duration) reason = TASK_WAIT_TIMEOUT;
    else {
        /* Tick capture, sequence check and registration form one update. An
           IRQ cannot pass a deadline or signal the event between them. */
        current->waiting = event;
        current->deadline_active = timed;
        current->deadline = timed ? timer_ticks() + duration : 0;
        current->state = TASK_BLOCKED;
        schedule();
        if (current->waiting || current->deadline_active || current->deadline) cpu_halt();
        reason = task_current_cancelled() ? TASK_WAIT_CANCELLED : current->wake_reason;
    }
    cpu_interrupt_restore(saved);
    return reason;
}

bool task_wait(struct task_event *event, uint32_t observed)
{
    /* Existing cancellable callers recheck their predicate/cancellation flag.
       Do not terminate here: their kernel frames may own temporary resources. */
    return wait_common(event, observed, false, 0) != TASK_WAIT_INVALID;
}

enum task_wait_result task_wait_for(struct task_event *event, uint32_t observed, uint32_t duration)
{
    return wait_common(event, observed, true, duration);
}

rum_result_t task_sleep(uint32_t milliseconds)
{
    uint32_t duration;
    if (!timer_duration_ticks(milliseconds, &duration)) return -RUM_ERANGE;
    if (!duration) {
        if (!task_yield()) return -RUM_EBUSY;
        return task_current_cancelled() ? -RUM_EINTR : 0;
    }
    enum task_wait_result reason = task_wait_for(NULL, 0, duration);
    return reason == TASK_WAIT_TIMEOUT ? 0 : reason == TASK_WAIT_CANCELLED ? -RUM_EINTR : -RUM_EBUSY;
}

struct task_event *task_work_event(void)
{
    return &work_event;
}

static _Noreturn void start_task(void)
{
    (void)reap_exited(false);
    if (current->process->kind == TASK_PROCESS) {
        task_cancel_current_if_requested();
        interrupt_enter(&current->user_frame);
    }
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
