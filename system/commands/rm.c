#include <rum/tools.h>
int main(int argc, char **argv)
{
    if (argc != 2) { tool_print("Usage: rm <path>\n"); return 1; }
    rum_result_t result = rum_remove(argv[1]);
    if (result < 0) { tool_error("rm", result); return 1; }
    return 0;
}
