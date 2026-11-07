#ifndef RUM_JOBS_H
#define RUM_JOBS_H
#include <rum/abi/job.h>
#include <rum/task.h>
/* Fixed session-owned results survive task reaping. task.c controls
   authorization, publication, console focus and actual cancellation. */
struct kernel_job {
    uint32_t owner, tasks[RUM_JOB_STAGE_LIMIT], operations;
    bool building, abandoned, cancelled;
    struct task_event event;
    struct rum_job_entry entry;
};
rum_result_t jobs_create(uint32_t owner, const char *name, unsigned flags, struct kernel_job **);
struct kernel_job *jobs_find(uint32_t owner, uint32_t id);
struct kernel_job *jobs_by_id(uint32_t id); /* Trusted kernel use only. */
struct kernel_job *jobs_building(uint32_t owner);
struct kernel_job *jobs_for_task(uint32_t task);
unsigned jobs_list(uint32_t owner, struct rum_job_entry *, unsigned capacity, uint32_t *total);
struct job_statistics { uint32_t jobs, running, completed, operations; };
struct job_statistics jobs_stats(void);
bool jobs_join(struct kernel_job *, uint32_t task, uint32_t pid);
void jobs_commit(struct kernel_job *);
void jobs_cancel(struct kernel_job *);
void jobs_exit(struct kernel_job *, uint32_t task, const struct rum_process_result *);
void jobs_abandon(uint32_t owner);
void jobs_reclaim(struct kernel_job *);
bool jobs_release(struct kernel_job *);
#endif
