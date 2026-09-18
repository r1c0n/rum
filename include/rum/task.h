#ifndef RUM_TASK_H
#define RUM_TASK_H
#include <stdbool.h>
#include <stdint.h>
#include <rum/paging.h>

typedef uint32_t task_id;
enum task_state { TASK_UNUSED, TASK_RUNNABLE, TASK_RUNNING, TASK_BLOCKED, TASK_EXITED };
struct task_event { volatile uint32_t sequence; };
struct task_information {
    task_id id;
    enum task_state state;
    uint32_t stack_base, stack_top, saved_stack, directory;
    bool owns_space;
};

/* Once, on the boot stack after paging/heap. Registers boot and idle contexts. */
bool task_initialize(void);
/* Foreground with IF enabled. NULL space borrows the kernel directory.
   A registered inactive private space transfers ownership only on success.
   Kernel entries return normally to exit; there is no timer preemption. */
task_id task_create(void (*entry)(void *), void *argument, struct paging_space *owned_space);
task_id task_current_id(void);
bool task_query(task_id id, struct task_information *information);
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
