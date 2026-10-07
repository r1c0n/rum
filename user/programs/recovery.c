#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    rum_result_t result = rum_session(RUM_SESSION_RECOVERY, 0);
    if (result < 0) { tool_error("recovery", result); return 1; }
    return 0;
}
