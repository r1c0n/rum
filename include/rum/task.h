#ifndef RUM_TASK_H
#define RUM_TASK_H
#include <stdbool.h>
#include <stdint.h>
#include <rum/paging.h>
#include <rum/process_limits.h>

#define RUM_TASK_CAPACITY (RUM_PROCESS_LIMIT + 2u)

typedef uint32_t task_id;
enum task_state { TASK_UNUSED, TASK_RUNNABLE, TASK_RUNNING, TASK_BLOCKED, TASK_EXITED };
struct task_event { volatile uint32_t sequence; };
struct task_information {
    task_id id;
    enum task_state state;
    uint32_t stack_slot, stack_base, stack_top, saved_stack, directory;
    bool owns_space, owns_stack;
};

struct task_snapshot {
    task_id current;
    uint32_t count, states[TASK_EXITED + 1];
    uint32_t stack_pages, directory_pages;
    uint32_t created, exited, reaped, switches;
    struct task_information tasks[RUM_TASK_CAPACITY];
};

/* Once, on the boot stack after paging/heap. Registers boot and idle contexts. */
bool task_initialize(void);
/* Foreground with IF enabled. NULL space borrows the kernel directory.
   A registered inactive private space transfers ownership only on success.
   Kernel entries return normally to exit; there is no timer preemption. */
task_id task_create(void (*entry)(void *), void *argument, struct paging_space *owned_space);
task_id task_current_id(void);
bool task_query(task_id id, struct task_information *information);
/* Bounded, allocation-free and IRQ-safe. Copies one consistent registry view;
   false before initialization (with a zeroed result) or for a NULL result. */
bool task_snapshot_read(struct task_snapshot *snapshot);
bool task_yield(void); /* Refuses IRQ context and callers with IF clear. */
_Noreturn void task_exit(void); /* Only worker tasks; IF may be clear on exit. */
uint32_t task_reap(void); /* Foreground; only exited, inactive resources. */

/* Keep the event alive until all waiters resume. Capture its sequence before
   checking the associated queue/predicate, then wait with that snapshot.
   Signal wakes all waiters; resumed callers must recheck their predicate. */
uint32_t task_event_sequence(const struct task_event *event);
bool task_wait(struct task_event *event, uint32_t observed);
void task_event_signal(struct task_event *event); /* IRQ-safe; no switch/alloc/free. */
struct task_event *task_work_event(void); /* Timer/keyboard foreground events. */
#endif
