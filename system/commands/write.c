#include <rum/tools.h>
int main(int argc, char **argv)
{
    char text[RUM_COMMAND_TEXT_CAPACITY];
    if (argc < 2) { tool_print("Usage: write <path> [text]\n"); return 1; }
    rum_result_t result = tool_join(text, 2, argc, argv);
    if (result < 0) { tool_error("write", result); return 1; }
    result = rum_replace(argv[1], text, tool_length(text));
    if (result < 0) { tool_error("write", result); return 1; }
    return 0;
}
