#include "../lib/stream.h"
int main(int argc, char **argv)
{
    if (argc > 2) { tool_print("Usage: hexdump [path]\n"); return 1; }
    struct command_input input; rum_result_t result = command_input_open(&input, argc == 2 ? argv[1] : NULL);
    if (result < 0) { tool_error("hexdump", result); return 1; }
    const char hex[] = "0123456789ABCDEF"; uint64_t offset = 0;
    do {
        unsigned char bytes[16]; unsigned n = 0;
        while (n < sizeof bytes && (result = command_input_next(&input, bytes + n)) > 0) ++n;
        if (!n) break;
        char line[88]; unsigned used = 0, digits = offset > UINT32_MAX ? 16 : 8;
        for (unsigned i = digits; i; --i) line[used++] = hex[(offset >> ((i - 1) * 4)) & 15];
        line[used++] = ' '; line[used++] = ' ';
        for (unsigned i = 0; i < 16; ++i) {
            if (i == 8) line[used++] = ' ';
            line[used++] = i < n ? hex[bytes[i] >> 4] : ' ';
            line[used++] = i < n ? hex[bytes[i] & 15] : ' ';
            line[used++] = ' ';
        }
        line[used++] = '|';
        for (unsigned i = 0; i < n; ++i) line[used++] = bytes[i] >= 32 && bytes[i] <= 126 ? (char)bytes[i] : '.';
        line[used++] = '|'; line[used++] = '\n'; tool_output(line, used); offset += n;
    } while (result > 0);
    rum_result_t closed = command_input_close(&input);
    if (result < 0 || closed < 0) { tool_error("hexdump", result < 0 ? result : closed); return 1; }
    return 0;
}
