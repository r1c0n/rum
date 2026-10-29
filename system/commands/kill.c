#include <rum/tools.h>
int main(int argc, char **argv)
{
    uint32_t pid = 0;
    if (argc != 2 || !argv[1][0]) { tool_print("Usage: kill <pid>\n"); return 1; }
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || pid > (RUM_ABI_PID_MAX - (uint32_t)(*p - '0')) / 10u) {
            tool_error("kill", -RUM_EINVAL); return 1;
        }
        pid = pid * 10u + (uint32_t)(*p - '0');
    }
    if (!pid) { tool_error("kill", -RUM_EINVAL); return 1; }
    rum_result_t result = rum_terminate(pid, RUM_TERMINATE_SESSION);
    if (result < 0) { tool_error("kill", result); return 1; }
    tool_print("Cancellation requested for "); tool_number(pid); tool_print(".\n"); return 0;
}
