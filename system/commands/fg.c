#include <rum/tools.h>
int main(int argc, char **argv)
{
    uint32_t id;
    if (argc != 2 || tool_job_id(argv[1], &id) < 0) { tool_print("Usage: fg <job>\n"); return 1; }
    rum_result_t result = rum_job_foreground(id);
    if (result == -RUM_ESRCH) { tool_print("fg: job not found.\n"); return 1; }
    if (result < 0) { tool_error("fg", result); return 1; }
    tool_print("Foreground job "); tool_number((int32_t)id); tool_print(".\n");
    struct rum_job_entry entry;
    result = rum_job_wait(id, 0, &entry);
    if (result == 1) result = rum_job_release(id);
    if (result < 0) { tool_error("fg", result); return 1; }
    tool_child_status(&entry.result, 1); return 0;
}
