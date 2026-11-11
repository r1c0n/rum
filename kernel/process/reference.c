#include <rum/process_reference.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>

static struct process_reference references[PROCESS_REFERENCE_LIMIT];
static rum_result_t destroy(struct kernel_object *object)
{
    struct process_reference *reference = (void *)object;
    if (reference->attached) return -RUM_EBUSY;
    *reference = (struct process_reference){0};
    return 0;
}
static const struct object_type type = { OBJECT_PROCESS, destroy };
struct process_reference *process_reference_from_object(struct kernel_object *object)
{
    return object && object->type == &type ? (void *)object : NULL;
}
void process_references_reclaim(void)
{
    if (!task_blocking_allowed()) return;
    for (unsigned i = 0; i < PROCESS_REFERENCE_LIMIT; ++i) {
        struct process_reference *reference = &references[i];
        if (reference->object.type && !reference->attached && reference->object.owners[OBJECT_OWNER] &&
            object_release(&reference->object, OBJECT_OWNER)) cpu_halt();
    }
}
struct process_reference *process_reference_create(task_id id, const struct rum_process_object_information *info)
{
    if (!id || !info || !task_blocking_allowed()) return NULL;
    process_references_reclaim();
    for (unsigned i = 0; i < PROCESS_REFERENCE_LIMIT; ++i) if (!references[i].object.type) {
        struct process_reference *reference = &references[i];
        if (object_initialize(&reference->object, &type)) return NULL;
        reference->execution_id = id; reference->information = *info;
        reference->attached = true;
        if (object_publish(&reference->object, OBJECT_OWNER)) cpu_halt();
        return reference;
    }
    return NULL;
}
void process_reference_complete(struct process_reference *reference, const struct rum_process_object_information *info)
{
    if (!reference) return;
    rum_pid_t parent = reference->information.parent_id;
    reference->information = *info;
    reference->information.parent_id = parent;
    task_event_signal(&reference->event);
}
void process_reference_detach(struct process_reference *reference)
{
    if (!reference) return;
    reference->attached = false;
    if (reference->object.references > 1 && object_release(&reference->object, OBJECT_OWNER)) cpu_halt();
}
rum_result_t process_reference_wait(struct process_reference *reference, bool nohang, struct rum_process_object_information *info)
{
    for (;;) {
        if (task_current_cancelled()) return -RUM_EINTR;
        uint32_t observed = task_event_sequence(&reference->event);
        uint32_t saved = cpu_interrupt_save();
        bool done = reference->information.state == RUM_PROCESS_ENDED;
        if (done) *info = reference->information;
        cpu_interrupt_restore(saved);
        if (done) return 1;
        if (nohang) return 0;
        if (!task_wait(&reference->event, observed)) return -RUM_EBUSY;
    }
}
