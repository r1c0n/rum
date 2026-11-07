#include <assert.h>
#include <stdio.h>
#include <rum/jobs.h>
#include <rum/abi/error.h>

void task_event_signal(struct task_event *event) { ++event->sequence; }
static struct rum_process_result ended(uint32_t pid, uint32_t reason, int32_t status)
{
    return (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION,
        .size = sizeof(struct rum_process_result), .process_id = pid, .termination = reason, .status = status };
}
static void normal(void)
{
    struct kernel_job *job, *other;
    assert(!jobs_create(10, "cat | cat", RUM_JOB_FOREGROUND, &job));
    uint32_t id = job->entry.job_id;
    assert(jobs_create(10, "nested", 0, &other) == -RUM_EBUSY);
    assert(jobs_find(10, id) == job && !jobs_find(11, id) && jobs_building(10) == job);
    assert(jobs_join(job, 20, 100) && jobs_join(job, 21, 101));
    assert(jobs_for_task(20) == job && !jobs_release(job));
    struct rum_process_result b = ended(101, RUM_PROCESS_EXITED, -37), a = ended(100, RUM_PROCESS_EXITED, 5);
    jobs_exit(job, 21, &b); assert(job->entry.state == RUM_JOB_BUILDING && !jobs_release(job));
    jobs_commit(job); assert(job->entry.state == RUM_JOB_RUNNING);
    jobs_exit(job, 20, &a); assert(job->entry.state == RUM_JOB_DONE && job->entry.result.status == -37);
    assert(job->entry.stages[0].status == 5 && job->entry.stages[1].status == -37);
    struct rum_job_entry entries[8]; uint32_t total;
    assert(jobs_list(10, entries, 0, &total) == 0 && total == 1);
    assert(jobs_list(10, entries, 8, &total) == 1 && entries[0].job_id == id);
    assert(jobs_list(11, entries, 8, &total) == 0 && !total);
    assert(jobs_release(job) && !jobs_find(10, id));
    assert(!jobs_stats().jobs);
}
static void abnormal(void)
{
    struct kernel_job *job;
    assert(!jobs_create(30, "fault | spin", 0, &job));
    for (unsigned i = 0; i < RUM_JOB_STAGE_LIMIT; ++i) assert(jobs_join(job, 40 + i, 50 + i));
    assert(!jobs_join(job, 99, 99)); jobs_commit(job);
    struct rum_process_result result = ended(50, RUM_PROCESS_FAULTED, 0); result.fault_vector = 6;
    jobs_exit(job, 40, &result); assert(job->cancelled && job->entry.state == RUM_JOB_RUNNING);
    for (unsigned i = 1; i < RUM_JOB_STAGE_LIMIT; ++i) {
        result = ended(50 + i, RUM_PROCESS_CANCELLED, 0); jobs_exit(job, 40 + i, &result);
    }
    assert(job->entry.result.termination == RUM_PROCESS_FAULTED && job->entry.result.fault_vector == 6);
    assert(jobs_release(job));
}
static void limits_and_abandon(void)
{
    struct kernel_job *held[8], *extra;
    for (unsigned i = 0; i < RUM_JOB_LIMIT; ++i) {
        assert(!jobs_create(60, "done", 0, &held[i])); jobs_commit(held[i]);
    }
    assert(jobs_create(60, "full", 0, &extra) == -RUM_EJOBFULL && jobs_stats().completed == 8);
    uint32_t stale = held[0]->entry.job_id;
    assert(jobs_release(held[0]) && !jobs_create(60, "replacement", 0, &extra) && extra->entry.job_id != stale);
    assert(jobs_join(extra, 70, 80)); ++extra->operations; jobs_abandon(60);
    assert(!jobs_find(60, extra->entry.job_id) && jobs_stats().jobs == 1 && jobs_stats().operations == 1);
    struct rum_process_result result = ended(80, RUM_PROCESS_CANCELLED, 0);
    jobs_exit(extra, 70, &result); jobs_reclaim(extra);
    assert(jobs_stats().jobs == 1); --extra->operations; jobs_reclaim(extra);
    assert(!jobs_stats().jobs && !jobs_stats().operations);
}
int main(void)
{
    for (unsigned i = 0; i < 64; ++i) { normal(); abnormal(); limits_and_abandon(); }
    puts("PASS: bounded jobs, retained stage results, stale/session IDs, cancellation and active-wait lifetime");
    return 0;
}
