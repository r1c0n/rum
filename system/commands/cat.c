#include <rum/tools.h>
#include "../lib/stream.h"
int main(int argc, char **argv)
{
    int first = 1, binary = 0;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'b' && !argv[1][2]) { first = 2; binary = 1; }
    if (argc > first + 1) { tool_print("Usage: cat [-b] [path]\n"); return 1; }
    struct command_input input;
    rum_result_t result = command_input_open(&input, argc > first ? argv[first] : NULL);
    if (result < 0) { tool_error("cat", result); return 1; }
    result = command_copy(&input, UINT32_MAX, binary);
    rum_result_t closed = command_input_close(&input);
    if (result < 0 || closed < 0) { tool_error("cat", result < 0 ? result : closed); return 1; }
    return 0;
}
