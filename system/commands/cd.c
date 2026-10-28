#include <rum/tools.h>
int main(int argc, char **argv)
{
    if (argc > 2) { tool_print("Usage: cd [path]\n"); return 1; }
    rum_result_t result = rum_session(RUM_SESSION_CHDIR, argc == 2 ? argv[1] : "/");
    if (result < 0) { tool_error("cd", result); return 1; }
    return 0;
}
