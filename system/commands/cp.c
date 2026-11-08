#include "../lib/files.h"
int main(int argc, char **argv)
{
    int first = 1, replace = 0;
    if (argc > 1 && tool_length(argv[1]) == 2 && argv[1][0] == '-' && argv[1][1] == 'f') { replace = 1; ++first; }
    if (argc != first + 2 || argv[first][0] == '-') { tool_print("Usage: cp [-f] <source> <destination>\n"); return 1; }
    struct file_copy_status status;
    rum_result_t error = file_copy(argv[first], argv[first + 1], replace, &status);
    if (error < 0) { file_copy_error("cp", error, &status); return 1; }
    return 0;
}
