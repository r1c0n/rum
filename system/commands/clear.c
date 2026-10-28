#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    rum_result_t result = rum_console(RUM_CONSOLE_CLEAR);
    if (result < 0) { tool_error("clear", result); return 1; }
    return 0;
}
