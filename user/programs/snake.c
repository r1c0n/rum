#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    rum_result_t result = rum_console(RUM_CONSOLE_SNAKE);
    if (result < 0) { tool_error("snake", result); return 1; }
    return 0;
}
