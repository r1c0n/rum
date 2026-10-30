#include <rum/tools.h>
int main(int argc, char **argv)
{
    uint32_t milliseconds = 0;
    if (argc != 2 || !argv[1][0]) { tool_print("Usage: sleep <milliseconds>\n"); return 1; }
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || milliseconds > (RUM_SLEEP_MAX_MS - (uint32_t)(*p - '0')) / 10u) {
            tool_error("sleep", -RUM_EINVAL); return 1;
        }
        milliseconds = milliseconds * 10u + (uint32_t)(*p - '0');
    }
    rum_result_t result = rum_sleep(milliseconds);
    if (result < 0) { tool_error("sleep", result); return 1; }
    return 0;
}
