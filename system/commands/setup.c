#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: setup\n"); return 1; }
    const char *paths[] = {"/HOME", "/APPS"};
    for (unsigned i = 0; i < 2; ++i) {
        struct rum_file_information info;
        rum_result_t error = rum_stat(paths[i], &info);
        if (!error && info.kind != RUM_ENTRY_DIRECTORY) error = -RUM_ENOTDIR;
        if (error == -RUM_ENOENT) {
            error = rum_mkdir(paths[i]);
            if (error == -RUM_EEXIST) {
                error = rum_stat(paths[i], &info);
                if (!error && info.kind != RUM_ENTRY_DIRECTORY) error = -RUM_ENOTDIR;
            }
        }
        if (error < 0) { tool_error(paths[i], error); return 1; }
    }
    tool_print("Workspace ready: /HOME and /APPS.\n"); return 0;
}
