#include <rum/tools.h>
int main(int argc, char **argv)
{
    char text[RUM_COMMAND_TEXT_CAPACITY];
    rum_result_t result = tool_text(text, argc, argv);
    if (result < 0) { tool_error("write", result); return 1; }
    char *tail = text, *path = tool_word(&tail);
    if (!path) { tool_print("Usage: write <path> [text]\n"); return 1; }
    tail = tool_remainder(tail);
    result = rum_replace(path, tail, tool_length(tail));
    if (result < 0) { tool_error("write", result); return 1; }
    return 0;
}
