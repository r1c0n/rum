#include <rum/tools.h>
int main(int argc, char **argv)
{
    if (argc > 2) { tool_print("Usage: ls [path]\n"); return 1; }
    rum_result_t handle = rum_open(argc == 2 ? argv[1] : ".", RUM_OPEN_READ, RUM_OPEN_DIRECTORY);
    if (handle < 0) { tool_error("ls", handle); return 1; }
    struct rum_directory_entry entry = { .version = RUM_FS_ABI_VERSION, .size = sizeof entry };
    rum_result_t result;
    while ((result = rum_readdir((rum_handle_t)handle, &entry)) > 0) {
        tool_print(entry.name);
        if (entry.kind == RUM_ENTRY_DIRECTORY) tool_print("/");
        tool_print("\n");
    }
    rum_result_t closed = rum_close((rum_handle_t)handle);
    if (result < 0 || closed < 0) { tool_error("ls", result < 0 ? result : closed); return 1; }
    return 0;
}
