#include <rum/tools.h>
int main(int argc, char **argv)
{
    if (argc != 2) { tool_print("Usage: mkdir <path>\n"); return 1; }
    rum_result_t result = rum_mkdir(argv[1]);
    if (result < 0) { tool_error("mkdir", result); return 1; }
    return 0;
}
