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

static struct shell_editor editor;
static struct shell_matches matches;
static char prefix[RUM_ABI_PATH_CAPACITY + 2];
static unsigned prefix_bytes;
static void show_line(void)
{
    char text[RUM_TERM_MAX_LINE]; unsigned used = 0;
    for (unsigned i = 0; i < prefix_bytes; ++i) text[used++] = prefix[i];
    for (unsigned i = 0; i < editor.length; ++i) text[used++] = editor.line[i];
    rum_result_t result = rum_terminal_line(text, used, prefix_bytes + editor.cursor);
    if (result < 0) { tool_error("shell", result); rum_exit(1); }
}
static void begin_line(void)
{
    rum_result_t result = rum_getcwd(prefix, RUM_ABI_PATH_CAPACITY);
    if (result < 0) { tool_error("shell", result); rum_exit(1); }
    prefix_bytes = tool_length(prefix); prefix[prefix_bytes++] = '>'; prefix[prefix_bytes++] = ' ';
    prefix[prefix_bytes] = 0;
    result = rum_terminal_begin_inline();
    if (result < 0) { tool_error("shell", result); rum_exit(1); }
    show_line();
}
static void end_line(void)
{
    if (rum_terminal_end() < 0) rum_exit(1);
}
static void complete(void)
{
    struct shell_completion_context context;
    if (shell_completion_context(editor.line, editor.cursor, &context) < 0) return;
    rum_result_t result = shell_complete(&context, &matches);
    if (result < 0 || !matches.count) return;
    unsigned typed = tool_length(context.prefix);
    if (!matches.truncated && (matches.count == 1 || matches.common > typed)) {
        char replacement[RUM_COMMAND_TEXT_CAPACITY];
        unsigned bytes = matches.count == 1 ? tool_length(matches.values[0]) : matches.common;
        unsigned n = 0;
        for (unsigned i = 0; i < bytes; ++i) {
            char c = matches.values[0][i];
            if (c == ' ' || c == '\\' || c == '\'' || c == '"' || c == '|' || c == '&' || c == '<' || c == '>' || c == ';') {
                if (n == sizeof replacement - 1) return;
                replacement[n++] = '\\';
            }
            if (n == sizeof replacement - 1) return;
            replacement[n++] = c;
        }
        if (matches.count == 1 && n && replacement[n - 1] != '/' && context.end == editor.length && n < sizeof replacement - 1)
            replacement[n++] = ' ';
        replacement[n] = 0;
        if (shell_editor_replace(&editor, context.start, context.end, replacement)) show_line();
    } else {
        end_line(); tool_print("\n");
        for (unsigned i = 0; i < matches.count; ++i) { tool_print(matches.values[i]); tool_print("\n"); }
        if (matches.truncated) tool_print("Completion limit reached.\n");
        begin_line();
    }
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
    tool_print("Type 'help' for commands.\n");
    begin_line();
    for (;;) {
        struct rum_terminal_event event;
        rum_result_t result = rum_terminal_read(&event, 1, RUM_TERM_MAX_WAIT_MS, 0);
        if (result == -RUM_EINTR) {
            end_line(); completions(1);
            struct rum_terminal_capabilities caps;
            if (!rum_terminal_query(&caps) && caps.cursor_x) tool_print("\n");
            begin_line(); continue;
        }
        if (!result) continue;
        if (result != 1) return 1;
        if (event.type == RUM_TERM_KEY_EVENT && (event.value == RUM_KEY_PAGEUP || event.value == RUM_KEY_PAGEDOWN)) {
            if (rum_terminal_scroll(event.value == RUM_KEY_PAGEUP ? RUM_SCROLL_UP : RUM_SCROLL_DOWN) < 0) return 1;
            continue;
        }
        if (rum_terminal_scroll(RUM_SCROLL_LIVE) < 0) return 1;
        enum shell_edit_action action = shell_editor_key(&editor, event.value);
        if (action == SHELL_EDIT_ACCEPT) {
            end_line(); tool_print("\n");
            char command[RUM_COMMAND_TEXT_CAPACITY];
            for (unsigned i = 0; i <= editor.length; ++i) command[i] = editor.line[i];
            shell_editor_commit(&editor); execute(command); completions(0); begin_line();
        } else if (action == SHELL_EDIT_CANCEL) {
            end_line(); tool_print("^C\n"); completions(0); begin_line();
        } else if (action == SHELL_EDIT_COMPLETE) complete();
        else if (action == SHELL_EDIT_CHANGED) show_line();
    }
}
