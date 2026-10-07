#include <rum/user.h>

int main(int argc, char **argv);

static unsigned length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void output(const void *buffer, unsigned bytes)
{
    const char *s = buffer;
    while (bytes) {
        rum_result_t n = rum_write(RUM_STDOUT, s, bytes);
        if (n <= 0 || (unsigned)n > bytes) rum_exit(1);
        s += n; bytes -= (unsigned)n;
    }
}
static void print(const char *s) { output(s, length(s)); }
static void number(int32_t value)
{
    char digits[12]; unsigned n = 0, magnitude = (uint32_t)value;
    if (value < 0) { print("-"); magnitude = 0u - magnitude; }
    do { digits[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    while (n) output(&digits[--n], 1);
}
static void error(const char *command, rum_result_t result)
{
    const char *message;
    switch (-result) {
    case RUM_ENOENT: message = "file or directory not found"; break;
    case RUM_ENODEV: message = "disk is not mounted"; break;
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
    case RUM_EINVAL: message = "invalid path, name or arguments"; break;
    default: message = "operation failed"; break;
    }
    print(command); print(": "); print(message); print(".\n");
}
static char *word(char **text)
{
    while (**text == ' ' || **text == '\t') ++*text;
    if (!**text) return 0;
    char *start = *text;
    while (**text && **text != ' ' && **text != '\t') ++*text;
    if (**text) *(*text)++ = 0;
    return start;
}
static char *remainder(char *text)
{
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}
static void list(const char *path)
{
    rum_result_t handle = rum_open(path, RUM_OPEN_READ, RUM_OPEN_DIRECTORY);
    if (handle < 0) { error("ls", handle); return; }
    struct rum_directory_entry entry = { .version = RUM_FS_ABI_VERSION, .size = sizeof entry };
    rum_result_t result;
    while ((result = rum_readdir((rum_handle_t)handle, &entry)) > 0) {
        print(entry.name);
        if (entry.kind == RUM_ENTRY_DIRECTORY) print("/");
        print("\n");
    }
    if (result < 0) error("ls", result);
    result = rum_close((rum_handle_t)handle);
    if (result < 0) error("ls", result);
}
static void cat(const char *path)
{
    rum_result_t handle = rum_open(path, RUM_OPEN_READ, 0);
    if (handle < 0) { error("cat", handle); return; }
    unsigned char bytes[512]; rum_result_t result;
    int last = '\n';
    while ((result = rum_read((rum_handle_t)handle, bytes, sizeof bytes)) > 0) {
        /* Escape controls, including ESC, rather than let files alter the console. */
        for (rum_result_t i = 0; i < result; ++i) {
            unsigned c = bytes[i]; last = (int)c;
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\t') output(bytes + i, 1);
            else { const char hex[] = "0123456789ABCDEF";
                char escaped[] = {'\\', 'x', hex[c >> 4], hex[c & 15]}; output(escaped, 4); }
        }
    }
    if (last != '\n') print("\n");
    if (result < 0) error("cat", result);
    result = rum_close((rum_handle_t)handle);
    if (result < 0) error("cat", result);
}
static void run(char *text)
{
    struct rum_arguments arguments = {0};
    char *program = word(&text);
    if (!program) { print("Usage: run <program> [arguments]\n"); return; }
    char path[RUM_ABI_PATH_CAPACITY];
    unsigned size = length(program); int slash = 0;
    for (unsigned i = 0; i < size; ++i) if (program[i] == '/') slash = 1;
    unsigned prefix = slash ? 0 : 1;
    if (size + prefix >= sizeof path) { error("run", -RUM_ENAMETOOLONG); return; }
    if (prefix) path[0] = '/';
    for (unsigned i = 0; i <= size; ++i) path[i + prefix] = program[i];
    char *argument = program;
    do {
        size = length(argument) + 1;
        if (arguments.argc == RUM_ABI_ARGUMENT_LIMIT || size > RUM_ABI_ARGUMENT_BYTES - arguments.string_bytes) {
            error("run", -RUM_E2BIG); return;
        }
        arguments.offsets[arguments.argc++] = arguments.string_bytes;
        for (unsigned i = 0; i < size; ++i) arguments.strings[arguments.string_bytes++] = argument[i];
    } while ((argument = word(&text)));
    struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
    rum_result_t result = rum_run(path, &arguments, &child);
    if (result < 0) { error("run", result); return; }
    if (child.termination == RUM_PROCESS_CANCELLED) print("Program cancelled.\n");
    else if (child.termination == RUM_PROCESS_FAULTED) print("Program stopped after a user fault.\n");
    else { print("Program exited with status "); number(child.status); print(".\n"); }
}
static void execute(char *text)
{
    char *command = word(&text);
    if (!command) return;
    if (equal(command, "echo")) { print(remainder(text)); print("\n"); return; }
    if (equal(command, "run")) { run(text); return; }
    if (equal(command, "help")) {
        print("help              Show commands\n"
              "about             About rum\nclear             Clear the console\n"
              "echo <text>       Print text\nls [path]         List a directory\n"
              "cat <path>        Show a file (binary bytes escaped)\n"
              "pwd               Show the working directory\ncd [path]         Change directory\n"
              "write <path> [text] Create or replace a file\n"
              "mkdir <path>      Create a disk directory\nrm <path>         Remove a file or empty directory\n"
              "run <program> [arguments] Launch a foreground ELF; Ctrl+C cancels\n"
              "snake             Play Snake (WASD, P, R, Q)\n"
              "exit              Restart the shell\nrecovery          Enter the kernel recovery shell\n"); return;
    }
    if (equal(command, "about")) { print("rum OS v0.3.0\nAn island of our own.\nA small operating system for 32-bit x86.\n"); return; }
    if (equal(command, "exit")) rum_exit(0);
    if (equal(command, "recovery")) rum_exit(RUM_SHELL_RECOVERY_STATUS);
    rum_result_t result = 0;
    if (equal(command, "clear") || equal(command, "snake"))
        result = rum_console(equal(command, "clear") ? RUM_CONSOLE_CLEAR : RUM_CONSOLE_SNAKE);
    else if (equal(command, "pwd")) {
        char path[RUM_ABI_PATH_CAPACITY]; result = rum_getcwd(path, sizeof path);
        if (result >= 0) { print(path); print("\n"); }
    } else if (equal(command, "ls") || equal(command, "cd") || equal(command, "cat") ||
               equal(command, "write") || equal(command, "mkdir") || equal(command, "rm")) {
        char *path = word(&text);
        if (equal(command, "ls")) { list(path ? path : "."); return; }
        if (equal(command, "cd")) result = rum_chdir(path ? path : "/");
        else if (!path) { print("Usage: "); print(command); print(" <path>\n"); return; }
        else if (equal(command, "cat")) { cat(path); return; }
        else if (equal(command, "write")) { text = remainder(text); result = rum_replace(path, text, length(text)); }
        else if (equal(command, "mkdir")) result = rum_mkdir(path);
        else result = rum_remove(path);
    } else { print("Unknown command: "); print(command); print(". Type 'help' for commands.\n"); return; }
    if (result < 0) error(command, result);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char line[256]; unsigned used = 0;
    print("rum userspace shell. Type 'help' for commands.\n> ");
    for (;;) {
        char c; rum_result_t result = rum_read(RUM_STDIN, &c, 1);
        if (result != 1) return 1;
        if (c == '\n' || c == '\r') {
            print("\n"); line[used] = 0; execute(line); used = 0; print("> ");
        } else if (c == '\x03') { used = 0; print("^C\n> "); }
        else if (c == '\b' || c == 127) { if (used) { --used; print("\b \b"); } }
        else if (c == '\t') { for (unsigned i = 0; i < 4 && used < sizeof line - 1; ++i) { line[used++] = ' '; print(" "); } }
        else if (c >= 32 && c <= 126 && used < sizeof line - 1) { line[used++] = c; output(&c, 1); }
    }
}
