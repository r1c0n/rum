#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: sync\n"); return 1; }
    rum_result_t root = rum_open("/", RUM_OPEN_READ, RUM_OPEN_DIRECTORY);
    if (root < 0) { tool_error("sync", root); return 1; }
    rum_result_t error = rum_flush((rum_handle_t)root);
    rum_result_t closed = rum_close((rum_handle_t)root);
    if (!error) error = closed;
    if (error < 0) { tool_error("sync", error); return 1; }
    return 0;
}
