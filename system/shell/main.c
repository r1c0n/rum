#include "shell.h"

static struct { rum_pid_t pid; char name[RUM_PROCESS_NAME_CAPACITY]; } children[RUM_PROCESS_LIST_LIMIT];
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void completion(const struct rum_process_result *result)
{
    tool_print("["); tool_number(result->process_id); tool_print("] ");
    for (unsigned i = 0; i < RUM_PROCESS_LIST_LIMIT; ++i) if (children[i].pid == result->process_id) {
        tool_print(children[i].name); tool_print(": "); children[i].pid = 0; break;
    }
    if (result->termination == RUM_PROCESS_CANCELLED) tool_print("cancelled");
    else if (result->termination == RUM_PROCESS_FAULTED) tool_print("faulted");
    else { tool_print("exited with status "); tool_number(result->status); }
    tool_print(".\n");
}
static void completions(int editing)
{
    int shown = 0;
    for (;;) {
        struct rum_process_result result = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof result };
        rum_result_t done = rum_wait(RUM_WAIT_ANY, RUM_WAIT_NOHANG, &result);
        if (done != 1) break;
        int legacy = 0;
        for (unsigned i = 0; i < RUM_PROCESS_LIST_LIMIT; ++i) legacy |= children[i].pid == result.process_id;
        if (legacy) {
            if (editing && !shown) tool_print("\n");
            shown = 1; completion(&result);
        }
    }
    shell_job_notices(editing && !shown);
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
    unsigned first = 0;
    while (line[first] == ' ' || line[first] == '\t') ++first;
    if (!line[first]) return;
    struct shell_pipeline plan;
    rum_result_t result = shell_parse_pipeline(line, &plan);
    if (result < 0) { tool_print("shell: invalid or oversized command.\n"); return; }
    struct shell_stage *stage = &plan.stages[0];
    if (plan.count != 1 || stage->redirect_count || plan.background) { shell_run_pipeline(&plan); return; }
    char *command = stage->argv[0];
    if (equal(command, "exit") || equal(command, "recovery")) {
        if (stage->argc != 1) { tool_print("Usage: "); tool_print(command); tool_print("\n"); return; }
        rum_exit(equal(command, "recovery") ? RUM_SHELL_RECOVERY_STATUS : 0);
    }
    int background = equal(command, "start"); unsigned slot = 0;
    if (background) {
        if (stage->argc == 1) { tool_print("Usage: start <program> [arguments]\n"); return; }
        --stage->argc;
        for (unsigned i = 0; i < stage->argc; ++i) stage->argv[i] = stage->argv[i + 1];
        command = stage->argv[0];
        while (slot < RUM_PROCESS_LIST_LIMIT && children[slot].pid) ++slot;
        if (slot == RUM_PROCESS_LIST_LIMIT) { tool_print("start: too many owned processes.\n"); return; }
    }
    struct rum_arguments arguments; char path[RUM_ABI_PATH_CAPACITY];
    result = tool_arguments(&arguments, (int)stage->argc, stage->argv);
    if (!result) result = tool_program_path(command, path);
    struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
    if (!result) {
        result = background ? rum_spawn(path, &arguments) : rum_run_argv_command(path, &arguments, &child);
        if (result == -RUM_ENOENT) {
            int slash = 0;
            for (unsigned i = 0; command[i]; ++i) slash |= command[i] == '/';
            if (!slash) result = background ? rum_spawn(command, &arguments) : rum_run_argv_command(command, &arguments, &child);
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
