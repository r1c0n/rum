#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: reboot\n"); return 1; }
    rum_result_t result = rum_session(RUM_SESSION_REBOOT, NULL);
    if (result < 0) { tool_error("reboot", result); return 1; }
    return 0;
}
