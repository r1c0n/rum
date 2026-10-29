#include <rum/tools.h>
int main(int argc, char **argv)
{
    if (argc < 2) { tool_print("Usage: run <program> [arguments]\n"); return 1; }
    char path[RUM_ABI_PATH_CAPACITY];
    struct rum_arguments arguments;
    rum_result_t result = tool_program_path(argv[1], path);
    if (!result) result = tool_arguments(&arguments, argc - 1, argv + 1);
    struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
    if (!result) {
        result = rum_run(path, &arguments, &child);
        if (result == -RUM_ENOENT) {
            int slash = 0;
            for (unsigned i = 0; argv[1][i]; ++i) if (argv[1][i] == '/') slash = 1;
            if (!slash) result = rum_run(argv[1], &arguments, &child);
        }
    }
    if (result < 0) { tool_error("run", result); return 1; }
    tool_child_status(&child, 1);
    return 0;
}
