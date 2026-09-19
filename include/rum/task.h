#ifndef RUM_TASK_H
#define RUM_TASK_H
#include <stdbool.h>
#include <stdint.h>
#include <rum/interrupts.h>
#include <rum/paging.h>
#include <rum/process_limits.h>

#define RUM_TASK_CAPACITY (RUM_PROCESS_LIMIT + 2u)

typedef uint32_t task_id;
enum task_state { TASK_UNUSED, TASK_RUNNABLE, TASK_RUNNING, TASK_BLOCKED, TASK_EXITED };
enum task_kind { TASK_KERNEL, TASK_PROCESS };
struct task_event { volatile uint32_t sequence; };
struct task_resources {
    uint32_t kernel_stack_pages, directory_pages, user_table_pages, user_pages;
};
struct task_information {
    task_id id;
    rum_pid_t process_id;
    task_id parent;
    enum task_kind kind;
    enum task_state state;
    uint32_t stack_slot, stack_base, stack_top, saved_stack, directory;
    struct exception_user_frame user_frame;
    rum_result_t exit_status;
    struct task_resources resources;
    bool owns_space, owns_stack;
};

/* A trusted kernel producer supplies a completely mapped address space and
   initial user frame. The entry is the temporary kernel bootstrap used until
   ring-3 restore lands; ownership transfers only when publication succeeds. */
struct task_process {
    void (*entry)(void *);
    void *argument;
    struct paging_space *space;
    struct exception_user_frame user_frame;
};

struct task_snapshot {
    task_id current;
    uint32_t count, processes, states[TASK_EXITED + 1];
    uint32_t stack_pages, emergency_stack_pages, directory_pages;
    uint32_t user_table_pages, user_pages;
    uint32_t created, exited, reaped, switches;
    struct task_information tasks[RUM_TASK_CAPACITY];
};

/* Once, on the boot stack after paging/heap. Registers boot and idle contexts. */
bool task_initialize(void);
/* Foreground with IF enabled. NULL space borrows the kernel directory.
   A registered inactive private space transfers ownership only on success.
   Kernel entries return normally to exit; there is no timer preemption. */
task_id task_create(void (*entry)(void *), void *argument, struct paging_space *owned_space);
task_id task_create_process(const struct task_process *process);
task_id task_current_id(void);
bool task_query(task_id id, struct task_information *information);
/* Bounded, allocation-free and IRQ-safe. Copies one consistent registry view;
   false before initialization (with a zeroed result) or for a NULL result. */
bool task_snapshot_read(struct task_snapshot *snapshot);
bool task_yield(void); /* Refuses IRQ context and callers with IF clear. */
_Noreturn void task_exit(void); /* Only worker tasks; equivalent to status zero. */
_Noreturn void task_exit_with_status(rum_result_t status); /* IF may be clear. */
uint32_t task_reap(void); /* Foreground; only exited, inactive resources. */

/* Keep the event alive until all waiters resume. Capture its sequence before
   checking the associated queue/predicate, then wait with that snapshot.
   Signal wakes all waiters; resumed callers must recheck their predicate. */
uint32_t task_event_sequence(const struct task_event *event);
bool task_wait(struct task_event *event, uint32_t observed);
void task_event_signal(struct task_event *event); /* IRQ-safe; no switch/alloc/free. */
struct task_event *task_work_event(void); /* Timer/keyboard foreground events. */
#endif
