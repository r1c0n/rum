#include <rum/tools.h>
int main(int argc, char **argv)
{
    int first = 1, binary = 0;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'b' && !argv[1][2]) { first = 2; binary = 1; }
    if (argc > first + 1) { tool_print("Usage: cat [-b] [path]\n"); return 1; }
    rum_result_t terminal = rum_isatty(RUM_STDOUT);
    if (terminal < 0) { tool_error("cat", terminal); return 1; }
    binary |= !terminal;
    int own = argc > first;
    rum_result_t handle = own ? rum_open(argv[first], RUM_OPEN_READ, 0) : RUM_STDIN;
    if (handle < 0) { tool_error("cat", handle); return 1; }
    unsigned char bytes[512]; rum_result_t result;
    int last = '\n', pending_cr = 0;
    while ((result = rum_read((rum_handle_t)handle, bytes, sizeof bytes)) > 0) {
        if (binary) { tool_output(bytes, (unsigned)result); continue; }
        /* Display CRLF as one newline, retaining a CR across read boundaries.
           Escape lone controls so file contents cannot alter the console. */
        for (rum_result_t i = 0; i < result; ++i) {
            unsigned c = bytes[i]; last = (int)c;
            if (pending_cr && c != '\n') tool_print("\\x0D");
            pending_cr = c == '\r';
            if (pending_cr) continue;
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\t') tool_output(bytes + i, 1);
            else { const char hex[] = "0123456789ABCDEF";
                char escaped[] = {'\\', 'x', hex[c >> 4], hex[c & 15]}; tool_output(escaped, 4); }
        }
    }
    if (pending_cr) tool_print("\\x0D");
    if (!binary && last != '\n') tool_print("\n");
    rum_result_t closed = own ? rum_close((rum_handle_t)handle) : 0;
    if (result < 0 || closed < 0) { tool_error("cat", result < 0 ? result : closed); return 1; }
    return 0;
}
