#include <rum/task.h>
#include <rum/cpu.h>
#include <stdlib.h>
#include "pipe-task-backend.h"

bool pipe_test_cancelled;
bool task_blocking_allowed(void) { return !irq_in_handler() && (handle_test_flags & 0x200u); }
_Noreturn void cpu_halt(void) { abort(); }
void (*pipe_test_before_wait)(void);
bool task_current_cancelled(void) { return pipe_test_cancelled; }
uint32_t task_event_sequence(const struct task_event *event) { return event->sequence; }
void task_event_signal(struct task_event *event) { ++event->sequence; }
bool task_wait(struct task_event *event, uint32_t observed)
{
    if (pipe_test_before_wait) {
        void (*hook)(void) = pipe_test_before_wait;
        pipe_test_before_wait = NULL; hook();
    }
    return event->sequence != observed || pipe_test_cancelled;
}
