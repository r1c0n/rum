#include "../lib/pager.h"
int main(int argc, char **argv)
{
    if (argc > 2 || ((argc == 1 || command_equal(argv[1], "-")) && rum_isatty(RUM_STDIN) == 1)) {
        tool_print("Usage: more [path]\n"); return 1;
    }
    struct command_input input;
    rum_result_t result = command_input_open(&input, argc == 2 ? argv[1] : NULL);
    if (result < 0) { tool_error("more", result); return 1; }
    struct pager_file file = { .input = &input };
    result = pager_view(argc == 2 ? argv[1] : "more", pager_file_next, &file);
    if (result == PAGER_FALLBACK) result = command_copy(&input, UINT32_MAX, 0);
    rum_result_t closed = command_input_close(&input);
    if (result < 0 || closed < 0) { tool_error("more", result < 0 ? result : closed); return 1; }
    return 0;
}
