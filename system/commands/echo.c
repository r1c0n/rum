#include <rum/tools.h>
int main(int argc, char **argv)
{
    char text[RUM_COMMAND_TEXT_CAPACITY];
    rum_result_t result = tool_text(text, argc, argv);
    if (result < 0) { tool_error("echo", result); return 1; }
    tool_print(text); tool_print("\n");
    return 0;
}
