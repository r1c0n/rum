#include <rum/tools.h>

static struct { rum_pid_t pid; char name[RUM_PROCESS_NAME_CAPACITY]; } children[RUM_PROCESS_LIST_LIMIT];
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void completions(int editing)
{
    int shown = 0;
    for (;;) {
        struct rum_process_result result = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof result };
        rum_result_t done = rum_wait(RUM_WAIT_ANY, RUM_WAIT_NOHANG, &result);
        if (done != 1) break;
        if (editing && !shown) tool_print("\n");
        shown = 1;
        tool_print("["); tool_number(result.process_id); tool_print("] ");
        for (unsigned i = 0; i < RUM_PROCESS_LIST_LIMIT; ++i) if (children[i].pid == result.process_id) {
            tool_print(children[i].name); tool_print(": "); children[i].pid = 0; break;
        }
        if (result.termination == RUM_PROCESS_CANCELLED) tool_print("cancelled");
        else if (result.termination == RUM_PROCESS_FAULTED) tool_print("faulted");
        else { tool_print("exited with status "); tool_number(result.status); }
        tool_print(".\n");
    }
}

static void prompt(void)
{
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t result = rum_getcwd(path, sizeof path);
    if (result < 0) { tool_error("shell", result); rum_exit(1); }
    tool_print(path); tool_print("> ");
}

static void execute(char *line)
{
    char *tail = line, *argv[RUM_ABI_ARGUMENT_LIMIT];
    char *command = tool_word(&tail);
    if (!command) return;
    int background = equal(command, "start");
    unsigned slot = 0;
    if (background) {
        command = tool_word(&tail);
        if (!command) { tool_print("Usage: start <program> [arguments]\n"); return; }
        while (slot < RUM_PROCESS_LIST_LIMIT && children[slot].pid) ++slot;
        if (slot == RUM_PROCESS_LIST_LIMIT) { tool_print("start: too many owned processes.\n"); return; }
    }
    char text[RUM_COMMAND_TEXT_CAPACITY];
    tail = tool_remainder(tail);
    unsigned size = tool_length(tail);
    for (unsigned i = 0; i <= size; ++i) text[i] = tail[i];
    int argc = 1;
    argv[0] = command;
    char *argument;
    while ((argument = tool_word(&tail))) {
        if (argc == RUM_ABI_ARGUMENT_LIMIT) { tool_error("shell", -RUM_E2BIG); return; }
        argv[argc++] = argument;
    }
    struct rum_arguments arguments;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t result = tool_arguments(&arguments, argc, argv);
    if (!result) result = tool_program_path(command, path);
    struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
    if (!result) {
        result = background ? rum_spawn(path, &arguments) : rum_run_command(path, &arguments, text, &child);
        /* Bare names search /rum first, then the working directory. Explicit
           paths bypass the search. The kernel supplies the optional .elf suffix. */
        if (result == -RUM_ENOENT) {
            int slash = 0;
            for (unsigned i = 0; command[i]; ++i) if (command[i] == '/') slash = 1;
            if (!slash) result = background ? rum_spawn(command, &arguments) : rum_run_command(command, &arguments, text, &child);
        }
    }
    if (result < 0) tool_error(command, result);
    else if (background) {
        children[slot].pid = (rum_pid_t)result;
        unsigned i = 0;
        while (command[i] && i < sizeof children[slot].name - 1) { children[slot].name[i] = command[i]; ++i; }
        children[slot].name[i] = 0;
        tool_print("["); tool_number(result); tool_print("] started "); tool_print(command); tool_print(".\n");
    } else tool_child_status(&child, 0);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char line[RUM_COMMAND_TEXT_CAPACITY]; unsigned used = 0;
    tool_print("Type 'help' for commands.\n");
    prompt();
    for (;;) {
        char c; rum_result_t result = rum_read(RUM_STDIN, &c, 1);
        if (result == -RUM_EINTR) {
            completions(1); prompt(); tool_output(line, used); continue;
        }
        if (result != 1) return 1;
        if (c == '\n' || c == '\r') {
            tool_print("\n"); line[used] = 0; execute(line); used = 0; completions(0); prompt();
        } else if (c == '\x03') { used = 0; tool_print("^C\n"); prompt(); }
        else if (c == '\b' || c == 127) { if (used) { --used; tool_print("\b \b"); } }
        else if (c == '\t') { for (unsigned i = 0; i < 4 && used < sizeof line - 1; ++i) { line[used++] = ' '; tool_print(" "); } }
        else if (c >= 32 && c <= 126 && used < sizeof line - 1) { line[used++] = c; tool_output(&c, 1); }
    }
}
