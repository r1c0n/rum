#include <rum/tools.h>

unsigned tool_length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
void tool_output(const void *buffer, unsigned bytes)
{
    const char *s = buffer;
    while (bytes) {
        rum_result_t n = rum_write(RUM_STDOUT, s, bytes);
        if (n <= 0 || (unsigned)n > bytes) rum_exit(1);
        s += n; bytes -= (unsigned)n;
    }
}
void tool_print(const char *s) { tool_output(s, tool_length(s)); }
void tool_number(int32_t value)
{
    char digits[12]; unsigned n = 0, magnitude = (uint32_t)value;
    if (value < 0) { tool_print("-"); magnitude = 0u - magnitude; }
    do { digits[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    while (n) tool_output(&digits[--n], 1);
}
void tool_error(const char *command, rum_result_t result)
{
    const char *message;
    switch (-result) {
    case RUM_ENOENT: message = "file or directory not found"; break;
    case RUM_ENODEV: message = "filesystem is not mounted"; break;
    case RUM_ENOTDIR: message = "not a directory"; break;
    case RUM_EISDIR: message = "is a directory"; break;
    case RUM_EROFS: message = "read-only filesystem"; break;
    case RUM_EACCES: message = "access denied"; break;
    case RUM_EEXIST: message = "already exists"; break;
    case RUM_ENOTEMPTY: message = "directory is not empty"; break;
    case RUM_EBUSY: message = "file or directory is in use"; break;
    case RUM_ENOMEM: message = "not enough memory"; break;
    case RUM_ENOSPC: message = "filesystem is full"; break;
    case RUM_EMFILE: message = "too many open files"; break;
    case RUM_ENAMETOOLONG: message = "path or name is too long"; break;
    case RUM_ENOEXEC: message = "not a supported executable"; break;
    case RUM_E2BIG: message = "file or argument list is too large"; break;
    case RUM_ETIMEDOUT: message = "disk did not respond"; break;
    case RUM_EIO: message = "I/O failure"; break;
    case RUM_EINVAL: message = "invalid path, name or arguments"; break;
    default: message = "operation failed"; break;
    }
    tool_print(command); tool_print(": "); tool_print(message); tool_print(".\n");
}
char *tool_word(char **text)
{
    while (**text == ' ' || **text == '\t') ++*text;
    if (!**text) return 0;
    char *start = *text;
    while (**text && **text != ' ' && **text != '\t') ++*text;
    if (**text) *(*text)++ = 0;
    return start;
}
char *tool_remainder(char *text)
{
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}
rum_result_t tool_arguments(struct rum_arguments *arguments, int argc, char **argv)
{
    *arguments = (struct rum_arguments){0};
    if (argc < 1 || argc > RUM_ABI_ARGUMENT_LIMIT) return -RUM_E2BIG;
    for (int i = 0; i < argc; ++i) {
        unsigned size = tool_length(argv[i]) + 1;
        if (size > RUM_ABI_ARGUMENT_BYTES - arguments->string_bytes) return -RUM_E2BIG;
        arguments->offsets[arguments->argc++] = arguments->string_bytes;
        for (unsigned j = 0; j < size; ++j) arguments->strings[arguments->string_bytes++] = argv[i][j];
    }
    return 0;
}
rum_result_t tool_program_path(const char *program, char path[RUM_ABI_PATH_CAPACITY])
{
    unsigned size = tool_length(program); int slash = 0;
    for (unsigned i = 0; i < size; ++i) if (program[i] == '/') slash = 1;
    unsigned prefix = slash ? 0 : 5;
    if (!size || size + prefix >= RUM_ABI_PATH_CAPACITY) return -RUM_ENAMETOOLONG;
    if (prefix) for (unsigned i = 0; i < prefix; ++i) path[i] = "/rum/"[i];
    for (unsigned i = 0; i <= size; ++i) path[i + prefix] = program[i];
    return 0;
}
rum_result_t tool_text(char text[RUM_COMMAND_TEXT_CAPACITY], int argc, char **argv)
{
    rum_result_t result = rum_command_text(text, RUM_COMMAND_TEXT_CAPACITY);
    if (result >= 0) return 0;
    if (result != -RUM_EACCES) return result;
    unsigned used = 0;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) { if (used == RUM_COMMAND_TEXT_CAPACITY - 1) return -RUM_E2BIG; text[used++] = ' '; }
        for (unsigned j = 0; argv[i][j]; ++j) {
            if (used == RUM_COMMAND_TEXT_CAPACITY - 1) return -RUM_E2BIG;
            text[used++] = argv[i][j];
        }
    }
    text[used] = 0;
    return 0;
}
void tool_child_status(const struct rum_process_result *child, int always)
{
    if (child->termination == RUM_PROCESS_CANCELLED) tool_print("Program cancelled.\n");
    else if (child->termination == RUM_PROCESS_FAULTED) tool_print("Program stopped after a user fault.\n");
    else if (always || child->status) {
        tool_print("Program exited with status "); tool_number(child->status); tool_print(".\n");
    }
}
