#include "../lib/stream.h"
int main(int argc, char **argv)
{
    unsigned flags = 0; int first = 1;
    for (; first < argc && argv[first][0] == '-' && argv[first][1]; ++first) {
        if (command_equal(argv[first], "--")) { ++first; break; }
        for (unsigned i = 1; argv[first][i]; ++i) {
            char c = argv[first][i];
            if (c == 'l') flags |= 1; else if (c == 'w') flags |= 2; else if (c == 'c') flags |= 4;
            else { tool_print("Usage: wc [-lwc] [path]\n"); return 1; }
        }
    }
    if (argc > first + 1) { tool_print("Usage: wc [-lwc] [path]\n"); return 1; }
    if (!flags) flags = 7;
    struct command_input input; rum_result_t result = command_input_open(&input, argc > first ? argv[first] : NULL);
    if (result < 0) { tool_error("wc", result); return 1; }
    uint64_t counts[3] = {0}; unsigned in_word = 0; unsigned char c;
    while ((result = command_input_next(&input, &c)) > 0) {
        ++counts[2]; counts[0] += c == '\n';
        unsigned space = c == ' ' || (c >= '\t' && c <= '\r');
        if (!space && !in_word) ++counts[1];
        in_word = !space;
    }
    rum_result_t closed = command_input_close(&input);
    if (result < 0 || closed < 0) { tool_error("wc", result < 0 ? result : closed); return 1; }
    unsigned printed = 0;
    for (unsigned i = 0; i < 3; ++i) if (flags & (1u << i)) {
        if (printed++) tool_print(" ");
        tool_unsigned(counts[i]);
    }
    if (argc > first) { tool_print(" "); tool_print(argv[first]); }
    tool_print("\n"); return 0;
}
