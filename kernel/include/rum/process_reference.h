#ifndef RUM_PROCESS_REFERENCE_H
#define RUM_PROCESS_REFERENCE_H
#include <rum/task.h>
#include <rum/abi/process-object.h>
#define PROCESS_REFERENCE_LIMIT 128u
/* Separate bounded status storage: never retains a task slot, page table,
   console membership, file table or kernel stack after execution is reaped. */
struct process_reference {
    struct kernel_object object;
    task_id execution_id;
    struct rum_process_object_information information;
    struct task_event event;
    bool attached;
};
struct process_reference *process_reference_create(task_id, const struct rum_process_object_information *);
struct process_reference *process_reference_from_object(struct kernel_object *);
void process_reference_complete(struct process_reference *, const struct rum_process_object_information *);
/* Reaper may detach with IF clear; final destruction is deferred until a safe
   task boundary. The execution-owner reference is released exactly once. */
void process_reference_detach(struct process_reference *);
void process_references_reclaim(void);
rum_result_t process_reference_wait(struct process_reference *, bool nohang, struct rum_process_object_information *);
#endif
