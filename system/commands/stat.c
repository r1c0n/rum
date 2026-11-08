#include <rum/tools.h>
int main(int argc, char **argv)
{
    if (argc != 2) { tool_print("Usage: stat <path>\n"); return 1; }
    struct rum_file_information info;
    rum_result_t error = rum_stat(argv[1], &info);
    if (error < 0) { tool_error("stat", error); return 1; }
    tool_print("Path: "); tool_print(argv[1]);
    tool_print(info.kind == RUM_ENTRY_FILE ? "\nType: file\nSize: " : "\nType: directory\nSize: ");
    tool_unsigned((uint64_t)info.size_lo | (uint64_t)info.size_hi << 32); tool_print(" bytes\nVolume: ");
    tool_print(info.flags & RUM_INFO_READ_ONLY_VOLUME ? "read-only\n" : "read/write\n");
    tool_print("Timestamps: unavailable\n"); return 0;
}
