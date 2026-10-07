#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    rum_result_t result = rum_session(RUM_SESSION_EXIT, 0);
    if (result < 0) { tool_error("exit", result); return 1; }
    return 0;
}
