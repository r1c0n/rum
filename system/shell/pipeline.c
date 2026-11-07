#include "shell.h"

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void close_owned(rum_result_t *handles, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) if (handles[i] >= 0) {
        (void)rum_close((rum_handle_t)handles[i]); handles[i] = -1;
    }
}
static void stop(rum_pid_t *pids, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) if (pids[i]) (void)rum_terminate(pids[i], 0);
}
void shell_run_pipeline(struct shell_pipeline *plan, void (*completion)(const struct rum_process_result *))
{
    /* Session-changing commands need the legacy, synchronous command authority.
       Reject them before opening or truncating any redirection target. */
    for (unsigned i = 0; i < plan->count; ++i) {
        const char *name = plan->stages[i].argv[0];
        if (equal(name, "start") || equal(name, "cd") || equal(name, "exit") || equal(name, "recovery")) {
            tool_print("shell: session commands cannot use pipes or redirection.\n"); return;
        }
    }
    rum_result_t error = rum_session(RUM_SESSION_PIPELINE_BEGIN, 0);
    if (error < 0) { tool_error("shell", error); return; }
    rum_result_t pipes[(SHELL_PIPELINE_LIMIT - 1) * 2];
    rum_result_t files[SHELL_PIPELINE_LIMIT * 2];
    rum_handle_t streams[SHELL_PIPELINE_LIMIT][2];
    rum_pid_t pids[SHELL_PIPELINE_LIMIT] = {0};
    unsigned pipe_count = 0, started = 0;
    for (unsigned i = 0; i < SHELL_PIPELINE_LIMIT * 2; ++i) files[i] = -1;
    const char *failed = "shell";
    for (unsigned i = 1; i < plan->count; ++i) {
        struct rum_pipe_request pipe = { .version = RUM_PIPE_ABI_VERSION, .size = sizeof pipe };
        if ((error = rum_pipe(&pipe)) < 0) goto cleanup;
        pipes[pipe_count++] = (rum_result_t)pipe.read_handle;
        pipes[pipe_count++] = (rum_result_t)pipe.write_handle;
    }
    for (unsigned i = 0; i < plan->count; ++i) {
        streams[i][0] = i ? (rum_handle_t)pipes[(i - 1) * 2] : RUM_STDIN;
        streams[i][1] = i + 1 < plan->count ? (rum_handle_t)pipes[i * 2 + 1] : RUM_STDOUT;
        struct shell_stage *stage = &plan->stages[i];
        for (unsigned j = 0; j < stage->redirect_count; ++j) {
            struct shell_redirect *redirect = &stage->redirects[j];
            unsigned target = redirect->kind != SHELL_INPUT, slot = i * 2 + target;
            /* Repeated redirections are evaluated left to right, keeping only
               the last descriptor while preserving earlier filesystem effects. */
            if (files[slot] >= 0) { (void)rum_close((rum_handle_t)files[slot]); files[slot] = -1; }
            failed = redirect->path;
            unsigned flags = !target ? 0 : RUM_OPEN_CREATE |
                (redirect->kind == SHELL_APPEND ? RUM_OPEN_APPEND : RUM_OPEN_TRUNCATE);
            error = rum_open(redirect->path, target ? RUM_OPEN_WRITE : RUM_OPEN_READ, flags);
            if (error < 0) goto cleanup;
            files[slot] = error; streams[i][target] = (rum_handle_t)error;
        }
    }
    for (unsigned i = 0; i < plan->count; ++i) {
        struct shell_stage *stage = &plan->stages[i];
        struct rum_arguments arguments;
        failed = stage->argv[0];
        error = tool_arguments(&arguments, (int)stage->argc, stage->argv);
        if (error < 0) goto cleanup;
        char path[RUM_ABI_PATH_CAPACITY];
        error = tool_program_path(failed, path);
        if (error < 0) goto cleanup;
        struct rum_handle_mapping maps[] = {
            {streams[i][0], RUM_STDIN, {0}}, {streams[i][1], RUM_STDOUT, {0}}, {RUM_STDERR, RUM_STDERR, {0}}
        };
        error = rum_spawn_with_handles(path, &arguments, maps, 3, RUM_SPAWN_PIPELINE | RUM_SPAWN_NO_DEFAULT_STREAMS);
        if (error == -RUM_ENOENT) {
            int slash = 0;
            for (unsigned j = 0; failed[j]; ++j) slash |= failed[j] == '/';
            if (!slash) error = rum_spawn_with_handles(failed, &arguments, maps, 3,
                RUM_SPAWN_PIPELINE | RUM_SPAWN_NO_DEFAULT_STREAMS);
        }
        if (error < 0) goto cleanup;
        pids[started++] = (rum_pid_t)error;
    }
    error = 0;
cleanup:
    close_owned(pipes, pipe_count); close_owned(files, SHELL_PIPELINE_LIMIT * 2);
    if (error < 0) { tool_error(failed, error); stop(pids, started); }
    unsigned remaining = started;
    struct rum_process_result status = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof status };
    int abnormal = 0;
    while (remaining) {
        struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
        rum_result_t waited = rum_wait(RUM_WAIT_ANY, 0, &child);
        if (waited != 1) { tool_error("shell", waited); rum_exit(1); }
        unsigned i = 0;
        while (i < started && pids[i] != child.process_id) ++i;
        if (i == started) { completion(&child); continue; }
        pids[i] = 0; --remaining;
        if (child.termination != RUM_PROCESS_EXITED) {
            if (!abnormal || child.termination == RUM_PROCESS_FAULTED) status = child;
            abnormal = 1; stop(pids, started);
        } else if (i + 1 == started && !abnormal) status = child;
    }
    rum_result_t ended = rum_session(RUM_SESSION_PIPELINE_END, 0);
    if (ended < 0) { tool_error("shell", ended); rum_exit(1); }
    if (!error) tool_child_status(&status, 0);
}
