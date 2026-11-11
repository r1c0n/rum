#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: handles\n"); return 1; }
    struct rum_resource_information info;
    rum_result_t result = rum_resources(&info);
    if (result < 0) { tool_error("handles", result); return 1; }
    tool_print("This process: "); tool_number(info.descriptors); tool_print(" descriptors, ");
    tool_number(info.typed_handles); tool_print(" typed handles\n");
    tool_number(info.objects); tool_print(" objects, "); tool_number(info.table_references); tool_print(" table references\n");
    const char *names[] = {"", "files", "directories", "pipe readers", "pipe writers", "console inputs", "console outputs", "processes"};
    for (unsigned i = 1; i < RUM_RESOURCE_TYPE_COUNT; ++i) if (info.types[i]) {
        tool_print("  "); tool_print(names[i]); tool_print(": "); tool_number(info.types[i]); tool_print("\n");
    }
    return 0;
}
