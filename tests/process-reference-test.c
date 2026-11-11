#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/abi/error.h>
#include <rum/process_reference.h>

uint32_t handle_test_flags = 0x200;
static bool cancelled;
static struct process_reference *pending;
bool irq_in_handler(void) { return false; }
bool task_blocking_allowed(void) { return (handle_test_flags & 0x200) != 0; }
bool task_current_cancelled(void) { return cancelled; }
_Noreturn void cpu_halt(void) { abort(); }
uint32_t task_event_sequence(const struct task_event *event) { return event->sequence; }
void task_event_signal(struct task_event *event) { ++event->sequence; }
bool task_wait(struct task_event *event, uint32_t sequence)
{
    assert(event == &pending->event && event->sequence == sequence);
    struct rum_process_object_information info = pending->information;
    info.state = RUM_PROCESS_ENDED; info.termination = RUM_PROCESS_EXITED; info.status = -17;
    process_reference_complete(pending, &info);
    handle_test_flags = 0;
    process_reference_detach(pending); /* OPERATION keeps the result alive. */
    handle_test_flags = 0x200;
    return true;
}
int main(void)
{
    struct rum_process_object_information info = { .version = 1, .size = sizeof info,
        .process_id = 5, .parent_id = 3, .state = RUM_PROCESS_RUNNING }, output, saved;
    struct process_reference *reference = process_reference_create(7, &info);
    assert(reference && process_reference_from_object(&reference->object) == reference);
    assert(object_release(&reference->object, OBJECT_OWNER) == -RUM_EBUSY);
    memset(&output, 0x5a, sizeof output); saved = output;
    assert(!process_reference_wait(reference, true, &output) && !memcmp(&output, &saved, sizeof output));
    cancelled = true;
    assert(process_reference_wait(reference, false, &output) == -RUM_EINTR); cancelled = false;
    assert(!object_acquire(&reference->object, OBJECT_OPERATION)); pending = reference;
    assert(process_reference_wait(reference, false, &output) == 1 && output.status == -17 &&
           output.process_id == 5 && output.parent_id == 3 && !reference->attached);
    assert(process_reference_wait(reference, true, &output) == 1 && output.status == -17);
    assert(object_stats().total.objects == 1 && !object_stats().total.references[OBJECT_OWNER]);
    assert(!object_release(&reference->object, OBJECT_OPERATION) && !object_stats().total.objects);
    for (unsigned round = 0; round < 3; ++round) {
        struct process_reference *all[PROCESS_REFERENCE_LIMIT];
        for (unsigned i = 0; i < PROCESS_REFERENCE_LIMIT; ++i) assert((all[i] = process_reference_create(i + 1, &info)));
        assert(!process_reference_create(500, &info) && object_stats().total.objects == PROCESS_REFERENCE_LIMIT);
        handle_test_flags = 0;
        for (unsigned i = 0; i < PROCESS_REFERENCE_LIMIT; ++i) process_reference_detach(all[i]);
        process_references_reclaim(); assert(object_stats().total.objects == PROCESS_REFERENCE_LIMIT);
        handle_test_flags = 0x200;
        process_references_reclaim(); assert(!object_stats().total.objects);
    }
    puts("PASS: retained process results, non-consuming waits, cancellation, deferred destruction and bounded pool reuse");
}
