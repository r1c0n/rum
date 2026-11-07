#include <rum/jobs.h>
#include <rum/abi/error.h>
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/memory.h>
#include <rum/process_limits.h>

static struct kernel_job pool[RUM_PROCESS_LIMIT * RUM_JOB_LIMIT];
static uint32_t next_id = 1;
static bool live(const struct kernel_job *job)
{
    for (unsigned i = 0; i < job->entry.stage_count; ++i) if (job->tasks[i]) return true;
    return false;
}
static void refresh(struct kernel_job *job)
{
    job->entry.state = job->building ? RUM_JOB_BUILDING : live(job) ? RUM_JOB_RUNNING : RUM_JOB_DONE;
    if (job->cancelled) job->entry.flags |= RUM_JOB_CANCELLING;
    if (job->entry.state == RUM_JOB_DONE && job->entry.stage_count && !job->entry.result.termination)
        job->entry.result = job->entry.stages[job->entry.stage_count - 1];
    task_event_signal(&job->event);
}
struct kernel_job *jobs_find(uint32_t owner, uint32_t id)
{
    if (id) for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i)
        if (pool[i].entry.job_id == id && pool[i].owner == owner && !pool[i].abandoned) return &pool[i];
    return NULL;
}
struct kernel_job *jobs_building(uint32_t owner)
{
    for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i)
        if (pool[i].owner == owner && pool[i].building && !pool[i].abandoned) return &pool[i];
    return NULL;
}
struct kernel_job *jobs_by_id(uint32_t id)
{
    if (id) for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i)
        if (pool[i].entry.job_id == id && pool[i].owner) return &pool[i];
    return NULL;
}
struct kernel_job *jobs_for_task(uint32_t task)
{
    for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i) if (pool[i].owner)
        for (unsigned j = 0; j < pool[i].entry.stage_count; ++j) if (pool[i].tasks[j] == task) return &pool[i];
    return NULL;
}
rum_result_t jobs_create(uint32_t owner, const char *name, unsigned flags, struct kernel_job **out)
{
    if (!owner || !name || !out || (flags & ~RUM_JOB_FOREGROUND)) return -RUM_EINVAL;
    unsigned length = 0;
    while (length < RUM_JOB_NAME_CAPACITY && name[length]) {
        if ((unsigned char)name[length] < 32 || (unsigned char)name[length] > 126) return -RUM_EINVAL;
        ++length;
    }
    if (!length || length == RUM_JOB_NAME_CAPACITY) return -RUM_EINVAL;
    uint32_t saved = cpu_interrupt_save();
    unsigned owned = 0; struct kernel_job *slot = NULL;
    for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i) {
        if (pool[i].owner == owner && !pool[i].abandoned) {
            ++owned;
            if (pool[i].building) { cpu_interrupt_restore(saved); return -RUM_EBUSY; }
        }
        if (!pool[i].owner && !slot) slot = &pool[i];
    }
    rum_result_t error = owned == RUM_JOB_LIMIT ? -RUM_EJOBFULL : !slot ? -RUM_ENOMEM :
        !next_id ? -RUM_EOVERFLOW : 0;
    if (!error) {
        *slot = (struct kernel_job){ .owner = owner, .building = true,
            .entry = { .job_id = next_id, .state = RUM_JOB_BUILDING, .flags = flags } };
        next_id = next_id == RUM_ABI_PID_MAX ? 0 : next_id + 1;
        memcpy(slot->entry.name, name, length + 1); *out = slot;
    }
    cpu_interrupt_restore(saved); return error;
}
unsigned jobs_list(uint32_t owner, struct rum_job_entry *entries, unsigned capacity, uint32_t *total)
{
    uint32_t saved = cpu_interrupt_save(); unsigned count = 0; *total = 0;
    for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i)
        if (pool[i].owner == owner && !pool[i].abandoned) {
            ++*total; if (count < capacity) entries[count++] = pool[i].entry;
        }
    cpu_interrupt_restore(saved); return count;
}
bool jobs_join(struct kernel_job *job, uint32_t task, uint32_t pid)
{
    if (!job || !job->building || job->abandoned || !task || !pid || job->entry.stage_count == RUM_JOB_STAGE_LIMIT) return false;
    unsigned i = job->entry.stage_count++;
    job->tasks[i] = task;
    job->entry.stages[i] = (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION,
        .size = sizeof(struct rum_process_result), .process_id = pid };
    return true;
}
void jobs_commit(struct kernel_job *job) { job->building = false; refresh(job); }
void jobs_cancel(struct kernel_job *job) { job->cancelled = true; refresh(job); }
void jobs_exit(struct kernel_job *job, uint32_t task, const struct rum_process_result *result)
{
    for (unsigned i = 0; i < job->entry.stage_count; ++i) if (job->tasks[i] == task) {
        job->tasks[i] = 0; job->entry.stages[i] = *result;
        if (result->termination != RUM_PROCESS_EXITED) {
            job->cancelled = true;
            if (result->termination == RUM_PROCESS_FAULTED || job->entry.result.termination != RUM_PROCESS_FAULTED)
                job->entry.result = *result;
        }
        refresh(job); return;
    }
}
void jobs_reclaim(struct kernel_job *job)
{
    if (job->abandoned && !live(job) && !job->operations) *job = (struct kernel_job){0};
}
void jobs_abandon(uint32_t owner)
{
    for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i) if (pool[i].owner == owner) {
        pool[i].abandoned = true; pool[i].building = false; jobs_cancel(&pool[i]); jobs_reclaim(&pool[i]);
    }
}
bool jobs_release(struct kernel_job *job)
{
    if (!job || job->building || live(job) || job->operations) return false;
    *job = (struct kernel_job){0}; return true;
}
struct job_statistics jobs_stats(void)
{
    uint32_t saved = cpu_interrupt_save(); struct job_statistics result = {0};
    for (unsigned i = 0; i < sizeof pool / sizeof pool[0]; ++i) if (pool[i].owner) {
        ++result.jobs; result.operations += pool[i].operations;
        if (pool[i].entry.state == RUM_JOB_DONE) ++result.completed; else ++result.running;
    }
    cpu_interrupt_restore(saved); return result;
}
