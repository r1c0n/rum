#include <rum/tools.h>
static int collect(uint32_t id)
{
    struct rum_job_entry entry;
    rum_result_t result = rum_job_wait(id, 0, &entry);
    if (result == -RUM_ESRCH) { tool_print("wait: job not found.\n"); return 1; }
    if (result == 1) result = rum_job_release(id);
    if (result < 0) { tool_error("wait", result); return 1; }
    tool_print("Job "); tool_number((int32_t)id); tool_print(": ");
    tool_child_status(&entry.result, 1); return 0;
}
int main(int argc, char **argv)
{
    uint32_t id;
    if (argc > 2 || (argc == 2 && tool_job_id(argv[1], &id) < 0)) { tool_print("Usage: wait [job]\n"); return 1; }
    if (argc == 2) return collect(id);
    struct rum_job_entry entries[RUM_JOB_LIMIT]; uint32_t total;
    rum_result_t count = rum_job_list(entries, RUM_JOB_LIMIT, &total);
    if (count < 0) { tool_error("wait", count); return 1; }
    int status = 0;
    for (rum_result_t i = 0; i < count; ++i) status |= collect(entries[i].job_id);
    return status;
}
