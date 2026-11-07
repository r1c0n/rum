#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: jobs\n"); return 1; }
    struct rum_job_entry entries[RUM_JOB_LIMIT]; uint32_t total;
    rum_result_t count = rum_job_list(entries, RUM_JOB_LIMIT, &total);
    if (count < 0) { tool_error("jobs", count); return 1; }
    if (!count) tool_print("No jobs.\n");
    for (rum_result_t i = 0; i < count; ++i) {
        tool_print("["); tool_number((int32_t)entries[i].job_id); tool_print("] ");
        tool_print(entries[i].state == RUM_JOB_DONE ? "done  " : "running  ");
        tool_print(entries[i].name); tool_print(" | PIDs:");
        for (unsigned j = 0; j < entries[i].stage_count; ++j) {
            tool_print(" "); tool_number((int32_t)entries[i].stages[j].process_id);
        }
        tool_print("\n");
    }
    return 0;
}
