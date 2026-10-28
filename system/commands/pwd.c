#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: pwd\n"); return 1; }
    char path[RUM_ABI_PATH_CAPACITY]; rum_result_t result = rum_getcwd(path, sizeof path);
    if (result < 0) { tool_error("pwd", result); return 1; }
    tool_print(path); tool_print("\n");
    return 0;
}
