#include <rum/tools.h>

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
        result = rum_run_command(path, &arguments, text, &child);
        /* Bare names search /rum first, then the working directory. Explicit
           paths bypass the search. The kernel supplies the optional .elf suffix. */
        if (result == -RUM_ENOENT) {
            int slash = 0;
            for (unsigned i = 0; command[i]; ++i) if (command[i] == '/') slash = 1;
            if (!slash) result = rum_run_command(command, &arguments, text, &child);
        }
    }
    if (result < 0) tool_error(command, result);
    else tool_child_status(&child, 0);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char line[RUM_COMMAND_TEXT_CAPACITY]; unsigned used = 0;
    tool_print("rum userspace shell. Type 'help' for commands.\n");
    prompt();
    for (;;) {
        char c; rum_result_t result = rum_read(RUM_STDIN, &c, 1);
        if (result != 1) return 1;
        if (c == '\n' || c == '\r') {
            tool_print("\n"); line[used] = 0; execute(line); used = 0; prompt();
        } else if (c == '\x03') { used = 0; tool_print("^C\n"); prompt(); }
        else if (c == '\b' || c == 127) { if (used) { --used; tool_print("\b \b"); } }
        else if (c == '\t') { for (unsigned i = 0; i < 4 && used < sizeof line - 1; ++i) { line[used++] = ' '; tool_print(" "); } }
        else if (c >= 32 && c <= 126 && used < sizeof line - 1) { line[used++] = c; tool_output(&c, 1); }
    }
}
