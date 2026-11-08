#include "../lib/files.h"
static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
int main(int argc, char **argv)
{
    int first = 1, replace = 0, copy = 0;
    while (first < argc && argv[first][0] == '-') {
        if (equal(argv[first], "-f") && !replace) replace = 1;
        else if (equal(argv[first], "--copy") && !copy) copy = 1;
        else break;
        ++first;
    }
    if (argc != first + 2 || argv[first][0] == '-') { tool_print("Usage: mv [-f] [--copy] <source> <destination>\n"); return 1; }
    if (!copy) {
        rum_result_t error = rum_rename(argv[first], argv[first + 1], replace ? RUM_RENAME_REPLACE : 0);
        if (error < 0) {
            tool_error("mv", error);
            if (error == -RUM_EXDEV) tool_print("Use mv --copy for an explicit copy-and-remove move.\n");
            return 1;
        }
    } else {
        struct file_copy_status status;
        rum_result_t error = file_copy(argv[first], argv[first + 1], replace, &status);
        if (error < 0) { file_copy_error("mv", error, &status); tool_print("mv: source was not removed.\n"); return 1; }
        error = rum_remove(argv[first]);
        if (error < 0) { tool_error("mv", error); tool_print("mv: copy completed; source removal failed.\n"); return 1; }
    }
    return 0;
}
