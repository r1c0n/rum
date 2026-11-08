#include <rum/tools.h>

unsigned tool_length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
void tool_output(const void *buffer, unsigned bytes)
{
    const char *s = buffer;
    while (bytes) {
        rum_result_t n = rum_write(RUM_STDOUT, s, bytes);
        if (n <= 0 || (unsigned)n > bytes) {
            tool_error("write", n < 0 ? n : -RUM_EIO); rum_exit(1);
        }
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
void tool_unsigned(uint64_t value)
{
    char digits[20]; unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
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
    case RUM_EPIPE: message = "pipe has no reader"; break;
    case RUM_ESPIPE: message = "stream does not support seeking"; break;
    case RUM_EJOBFULL: message = "job table is full; use wait to collect completed jobs"; break;
    case RUM_EXDEV: message = "paths are on different filesystems"; break;
    case RUM_EINVAL: message = "invalid path, name or arguments"; break;
    case RUM_ECHILD: message = "no matching child process"; break;
    case RUM_ESRCH: message = "process does not exist"; break;
    case RUM_EOVERFLOW: message = "value or process identifier is out of range"; break;
    default: message = "operation failed"; break;
    }
    const char *parts[] = {command, ": ", message, ".\n"};
    for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; ++i) {
        const char *p = parts[i]; unsigned bytes = tool_length(p);
        while (bytes) {
            rum_result_t written = rum_write(RUM_STDERR, p, bytes);
            if (written <= 0 || (unsigned)written > bytes) return;
            p += written; bytes -= (unsigned)written;
        }
    }
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
rum_result_t tool_job_id(const char *text, uint32_t *result)
{
    uint32_t id = 0;
    if (*text == '%') ++text;
    if (!*text) return -RUM_EINVAL;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || id > (RUM_ABI_PID_MAX - (uint32_t)(*text - '0')) / 10u) return -RUM_EINVAL;
        id = id * 10u + (uint32_t)(*text - '0');
    }
    if (!id) return -RUM_EINVAL;
    *result = id; return 0;
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
    return tool_join(text, 1, argc, argv);
}
rum_result_t tool_join(char text[RUM_COMMAND_TEXT_CAPACITY], int first, int argc, char **argv)
{
    unsigned used = 0;
    for (int i = first; i < argc; ++i) {
        if (i > first) { if (used == RUM_COMMAND_TEXT_CAPACITY - 1) return -RUM_E2BIG; text[used++] = ' '; }
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
