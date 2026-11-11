#include "../lib/pager.h"
struct entry { const char *group, *name, *usage, *description; };
static const struct entry entries[] = {
    {"files", "ls", "ls [path]", "List files and directories"},
    {"files", "pwd", "pwd", "Show the working directory"},
    {"files", "cd", "cd [path]", "Change the working directory"},
    {"files", "mkdir", "mkdir <path>", "Create a directory"},
    {"files", "rm", "rm <path>", "Remove a file or empty directory"},
    {"files", "touch", "touch <path>", "Create a file without replacing its contents"},
    {"files", "cp", "cp [-f] <src> <dst>", "Copy a file; -f permits replacement"},
    {"files", "mv", "mv [-f] [--copy] <src> <dst>", "Move a file; --copy permits a copy fallback"},
    {"files", "stat", "stat <path>", "Show file metadata and volume mode"},
    {"files", "setup", "setup", "Create /HOME and /APPS if missing"},
    {"files", "sync", "sync", "Flush the mounted volume"},
    {"text", "cat", "cat [-b] [path]", "Read a file or stdin; -b preserves console bytes"},
    {"text", "more", "more [path]", "Page through a text file"},
    {"text", "head", "head [-n count] [path]", "Read the first lines; default 10"},
    {"text", "wc", "wc [-lwc] [path]", "Count newlines, words and bytes"},
    {"text", "hexdump", "hexdump [path]", "Show offsets, hexadecimal bytes and text"},
    {"text", "echo", "echo [text]", "Print text followed by a newline"},
    {"text", "write", "write <path> [text]", "Create or replace a file with text"},
    {"processes", "run", "run <program> [arguments]", "Run a foreground program"},
    {"processes", "start", "start <program> [arguments]", "Start a program without waiting"},
    {"processes", "ps", "ps", "List processes"},
    {"processes", "kill", "kill <pid>", "Request process cancellation"},
    {"processes", "jobs", "jobs", "List shell jobs"},
    {"processes", "fg", "fg <job>", "Bring a job to the foreground"},
    {"processes", "wait", "wait [job]", "Wait for and collect jobs"},
    {"processes", "sleep", "sleep <milliseconds>", "Wait for the specified time"},
    {"shell", "help", "help [topic] [--all]", "Show commands, a group or one command"},
    {"shell", "clear", "clear", "Clear the visible console"},
    {"shell", "about", "about", "Show rum version information"},
    {"shell", "snake", "snake", "Play Snake; WASD move, P pause, R restart, Q quit"},
    {"shell", "exit", "exit", "Restart the shell"},
    {"shell", "shutdown", "shutdown", "Flush files and turn off the computer"},
    {"shell", "reboot", "reboot", "Flush files and restart the computer"},
    {"shell", "recovery", "recovery", "Open the recovery shell"},
};
static char output[8192]; static unsigned used; static int overflow;
static void append(const char *s)
{
    while (*s) {
        if (used + 1 == sizeof output) { overflow = 1; return; }
        output[used++] = *s++;
    }
    output[used] = 0;
}
static int same(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == *b;
}
static void keys(void)
{
    append("Keys:\nArrows, Home/End and Delete edit the command.\n"
           "Up/Down browse history; Tab completes programs and paths.\n"
           "Page Up/Page Down browse console scrollback.\n"
           "Ctrl+L clears the screen; Ctrl+U clears the command.\n"
           "Ctrl+C cancels the foreground program or clears the prompt.\n");
}
static void syntax(void)
{
    append("Syntax:\ncat INPUT.TXT | head -n 5 > OUTPUT.TXT\n"
           "Use < for input, > to replace output, and >> to append.\n"
           "End a command or pipeline with & to create a background job.\n"
           "Single/double quotes group arguments; backslash escapes a character.\n"
           "Programs are searched in /RUM, then the current directory.\n");
}
int main(int argc, char **argv)
{
    const char *topic = NULL; int plain = 0, found = 0;
    for (int i = 1; i < argc; ++i) {
        if (same(argv[i], "--all")) plain = 1;
        else if (!topic) topic = argv[i];
        else { tool_print("Usage: help [topic] [--all]\n"); return 1; }
    }
    used = 0; overflow = 0; output[0] = 0;
    if (!topic) append("rum commands\nUse help <command> for details; help --all prints without paging.\n\n");
    const char *group = "";
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; ++i) {
        const struct entry *entry = &entries[i];
        if (topic && !same(topic, entry->name) && !same(topic, entry->group)) continue;
        if (!same(group, entry->group)) { if (found) append("\n"); append(entry->group); append(":\n"); group = entry->group; }
        append(entry->usage); append("\n  "); append(entry->description); append("\n"); found = 1;
    }
    if (!topic || same(topic, "syntax")) { if (found) append("\n"); syntax(); found = 1; }
    if (!topic || same(topic, "keys")) { if (found) append("\n"); keys(); found = 1; }
    if (!found) { tool_print("help: unknown topic. Use files, text, processes, shell, keys or syntax.\n"); return 1; }
    if (overflow) { tool_error("help", -RUM_E2BIG); return 1; }
    if (!plain) {
        struct pager_text text = { output, 0 };
        rum_result_t result = pager_view("rum help", pager_text_next, &text);
        if (result < 0) { tool_error("help", result); return 1; }
        if (!result) return 0;
    }
    tool_print(output); return 0;
}
