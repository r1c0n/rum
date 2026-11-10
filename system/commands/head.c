#include "../lib/stream.h"
int main(int argc, char **argv)
{
    uint32_t lines = 10; int first = 1;
    if (argc > 1 && command_equal(argv[1], "-n")) {
        if (argc < 3 || command_count(argv[2], &lines) < 0) { tool_print("Usage: head [-n count] [path]\n"); return 1; }
        first = 3;
    }
    if (argc > first + 1) { tool_print("Usage: head [-n count] [path]\n"); return 1; }
    struct command_input input;
    rum_result_t result = command_input_open(&input, argc > first ? argv[first] : NULL);
    if (result < 0) { tool_error("head", result); return 1; }
    result = command_copy(&input, lines, 0);
    rum_result_t closed = command_input_close(&input);
    if (result < 0 || closed < 0) { tool_error("head", result < 0 ? result : closed); return 1; }
    return 0;
}
