#include <rum/tools.h>
static rum_result_t existing(const char *path)
{
    struct rum_file_information info;
    rum_result_t error = rum_stat(path, &info);
    return error ? error : info.kind == RUM_ENTRY_FILE ? 0 : -RUM_EISDIR;
}
int main(int argc, char **argv)
{
    if (argc != 2) { tool_print("Usage: touch <path>\n"); return 1; }
    rum_result_t error = existing(argv[1]);
    if (error == -RUM_ENOENT) {
        rum_result_t file = rum_open(argv[1], RUM_OPEN_WRITE, RUM_OPEN_CREATE | RUM_OPEN_EXCLUSIVE);
        if (file == -RUM_EEXIST) error = existing(argv[1]);
        else if (file < 0) error = file;
        else {
            error = rum_flush((rum_handle_t)file);
            rum_result_t closed = rum_close((rum_handle_t)file); if (!error) error = closed;
        }
    }
    if (error < 0) { tool_error("touch", error); return 1; }
    return 0;
}
