#include "shell.h"

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void append(char *line, unsigned *used, const char *text)
{
    while (*text) line[(*used)++] = *text++;
}
static void number(char *line, unsigned *used, int32_t value)
{
    uint32_t magnitude = (uint32_t)value; char digits[10]; unsigned count = 0;
    if (value < 0) { line[(*used)++] = '-'; magnitude = 0u - magnitude; }
    do { digits[count++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    while (count) line[(*used)++] = digits[--count];
}
static void notice(uint32_t id, const char *name, const struct rum_process_result *result, int editing)
{
    /* The bounded name and two signed 32-bit numbers fit one 128-byte console
       write, so background output cannot split the notice's identity. */
    char line[128]; unsigned used = 0;
    _Static_assert(RUM_JOB_NAME_CAPACITY + 48 <= sizeof line, "job notice fits a console chunk");
    if (editing) line[used++] = '\n';
    append(line, &used, "["); number(line, &used, (int32_t)id); append(line, &used, "] ");
    if (!result) { append(line, &used, "started "); append(line, &used, name); }
    else {
        append(line, &used, name); append(line, &used, ": ");
        if (result->termination == RUM_PROCESS_FAULTED) append(line, &used, "faulted");
        else if (result->termination == RUM_PROCESS_CANCELLED) append(line, &used, "cancelled");
        else { append(line, &used, "exited with status "); number(line, &used, result->status); }
    }
    append(line, &used, ".\n"); tool_output(line, used);
}
static void close_owned(rum_result_t *handles, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) if (handles[i] >= 0) {
        (void)rum_close((rum_handle_t)handles[i]); handles[i] = -1;
    }
}
void shell_run_pipeline(struct shell_pipeline *plan)
{
    /* Session-changing commands need direct synchronous command authority.
       Reject them before opening or truncating any redirection target. */
    for (unsigned i = 0; i < plan->count; ++i) {
        const char *name = plan->stages[i].argv[0];
        if (equal(name, "start") || equal(name, "cd") || equal(name, "exit") || equal(name, "recovery") ||
            equal(name, "jobs") || equal(name, "fg") || equal(name, "wait")) {
            tool_print("shell: session commands cannot use pipes or redirection.\n"); return;
        }
    }
    char name[RUM_JOB_NAME_CAPACITY]; unsigned used = 0;
    for (unsigned i = 0; i < plan->count; ++i) {
        if (i) for (unsigned j = 0; j < 3 && used < sizeof name - 1; ++j) name[used++] = " | "[j];
        for (unsigned j = 0; plan->stages[i].argv[0][j] && used < sizeof name - 1; ++j)
            name[used++] = plan->stages[i].argv[0][j];
    }
    name[used] = 0;
    rum_result_t created = rum_job_create(name, plan->background ? 0 : RUM_JOB_FOREGROUND);
    if (created < 0) { tool_error("shell", created); return; }
    uint32_t job = (uint32_t)created;
    rum_result_t error = 0;
    rum_result_t pipes[(SHELL_PIPELINE_LIMIT - 1) * 2];
    rum_result_t files[SHELL_PIPELINE_LIMIT * 2];
    rum_handle_t streams[SHELL_PIPELINE_LIMIT][2];
    unsigned pipe_count = 0;
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
        error = rum_spawn_with_handles(path, &arguments, maps, 3, RUM_SPAWN_JOB | RUM_SPAWN_NO_DEFAULT_STREAMS);
        if (error == -RUM_ENOENT) {
            int slash = 0;
            for (unsigned j = 0; failed[j]; ++j) slash |= failed[j] == '/';
            if (!slash) error = rum_spawn_with_handles(failed, &arguments, maps, 3,
                RUM_SPAWN_JOB | RUM_SPAWN_NO_DEFAULT_STREAMS);
        }
        if (error < 0) goto cleanup;
    }
    error = 0;
cleanup:
    close_owned(pipes, pipe_count); close_owned(files, SHELL_PIPELINE_LIMIT * 2);
    if (error < 0) {
        /* A stage can fault or be cancelled before later stages are spawned.
           EINTR describes that job result, not a failure to load this command. */
        if (error != -RUM_EINTR) tool_error(failed, error);
        if (rum_job_cancel(job) < 0) rum_exit(1);
    } else {
        error = rum_job_commit(job);
        if (error < 0) { tool_error("shell", error); if (rum_job_cancel(job) < 0) rum_exit(1); }
    }
    if (plan->background && !error) {
        notice(job, name, 0, 0); return;
    }
    struct rum_job_entry entry;
    rum_result_t waited = rum_job_wait(job, 0, &entry);
    if (waited != 1) { tool_error("shell", waited); rum_exit(1); }
    if (rum_job_release(job) < 0) rum_exit(1);
    if (!error || error == -RUM_EINTR) tool_child_status(&entry.result, 0);
}

void shell_job_notices(int editing)
{
    struct rum_job_entry entries[RUM_JOB_LIMIT]; uint32_t total;
    rum_result_t count = rum_job_list(entries, RUM_JOB_LIMIT, &total);
    if (count < 0) { tool_error("shell", count); return; }
    for (rum_result_t i = 0; i < count; ++i) if (entries[i].state == RUM_JOB_DONE && !(entries[i].flags & RUM_JOB_NOTIFIED)) {
        notice(entries[i].job_id, entries[i].name, &entries[i].result, editing); editing = 0;
        if (rum_job_ack(entries[i].job_id) < 0) rum_exit(1);
    }
}
