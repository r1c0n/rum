#include <rum/user.h>
static rum_result_t terminal_call(struct rum_terminal_request *request)
{
    request->version = RUM_TERMINAL_ABI_VERSION; request->size = sizeof *request;
    return rum_syscall3(RUM_SYS_TERMINAL, (rum_address_t)(uintptr_t)request, 0, 0);
}
rum_result_t rum_terminal_query(struct rum_terminal_capabilities *caps)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_QUERY, .data = (rum_address_t)(uintptr_t)caps };
    return terminal_call(&request);
}
rum_result_t rum_terminal_begin(void)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_BEGIN }; return terminal_call(&request);
}
rum_result_t rum_terminal_end(void)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_END }; return terminal_call(&request);
}
rum_result_t rum_terminal_begin_inline(void)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_BEGIN, .flags = RUM_TERM_INLINE };
    return terminal_call(&request);
}
rum_result_t rum_terminal_line(const char *text, uint32_t bytes, uint32_t cursor)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_LINE, .count = bytes, .x = cursor,
        .data = (rum_address_t)(uintptr_t)text }; return terminal_call(&request);
}
rum_result_t rum_terminal_scroll(uint32_t direction)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_SCROLL, .x = direction };
    return terminal_call(&request);
}
rum_result_t rum_terminal_cells(uint32_t x, uint32_t y, const struct rum_terminal_cell *cells, uint32_t count)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_CELLS, .x = x, .y = y, .count = count,
        .data = (rum_address_t)(uintptr_t)cells }; return terminal_call(&request);
}
rum_result_t rum_terminal_cursor(uint32_t x, uint32_t y, uint32_t visible)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_CURSOR, .x = x, .y = y, .flags = visible };
    return terminal_call(&request);
}
rum_result_t rum_terminal_color(uint32_t foreground, uint32_t background)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_COLOR, .x = foreground, .y = background };
    return terminal_call(&request);
}
rum_result_t rum_terminal_read(struct rum_terminal_event *events, uint32_t capacity, uint32_t milliseconds, uint32_t *ticks)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_READ, .count = capacity,
        .timeout_ms = milliseconds, .data = (rum_address_t)(uintptr_t)events };
    rum_result_t result = terminal_call(&request);
    if (result >= 0 && ticks) *ticks = request.now_ticks;
    return result;
}

rum_result_t rum_stat(const char *path, struct rum_file_information *output)
{
    struct rum_stat_request request = { .version = RUM_METADATA_ABI_VERSION, .size = sizeof request,
        .path = (rum_address_t)(uintptr_t)path, .data = (rum_address_t)(uintptr_t)output };
    return rum_syscall3(RUM_SYS_STAT, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_fstat(rum_handle_t handle, struct rum_file_information *output)
{
    struct rum_stat_request request = { .version = RUM_METADATA_ABI_VERSION, .size = sizeof request,
        .handle = handle, .flags = RUM_STAT_HANDLE, .data = (rum_address_t)(uintptr_t)output };
    return rum_syscall3(RUM_SYS_STAT, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_rename(const char *source, const char *destination, uint32_t flags)
{
    struct rum_rename_request request = { RUM_METADATA_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)source, (rum_address_t)(uintptr_t)destination, flags, 0 };
    return rum_syscall3(RUM_SYS_RENAME, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_pipe(struct rum_pipe_request *request)
{
    return rum_syscall3(RUM_SYS_PIPE, (rum_address_t)(uintptr_t)request, 0, 0);
}

rum_result_t rum_duplicate(rum_handle_t source, rum_handle_t target, uint32_t flags)
{
    struct rum_duplicate_request request = { RUM_HANDLE_ABI_VERSION, sizeof request, source, target, flags, 0 };
    return rum_syscall3(RUM_SYS_DUPLICATE, (rum_address_t)(uintptr_t)&request, 0, 0);
}

rum_result_t rum_spawn_with_handles(const char *path, const struct rum_arguments *arguments,
                                   const struct rum_handle_mapping *mappings, uint32_t count, uint32_t flags)
{
    struct rum_spawn_mapped_request request = { RUM_SPAWN_MAPPED_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)arguments,
        (rum_address_t)(uintptr_t)mappings, count, flags, 0 };
    return rum_syscall3(RUM_SYS_SPAWN_MAPPED, (rum_address_t)(uintptr_t)&request, 0, 0);
}

_Static_assert(sizeof(void *) == sizeof(rum_address_t), "i386 user pointers");

rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    struct rum_open_request request = { RUM_FS_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, access, flags, 0 };
    return rum_syscall3(RUM_SYS_OPEN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_close(rum_handle_t handle) { return rum_syscall3(RUM_SYS_CLOSE, handle, 0, 0); }
rum_result_t rum_seek(rum_handle_t handle, int64_t displacement, uint32_t whence, uint64_t *position)
{
    uint64_t bits = (uint64_t)displacement;
    struct rum_seek_request request = { RUM_FS_ABI_VERSION, sizeof request,
        (uint32_t)bits, (uint32_t)(bits >> 32), whence, 0, 0, 0 };
    rum_result_t result = rum_syscall3(RUM_SYS_SEEK, handle, (rum_address_t)(uintptr_t)&request, 0);
    if (!result && position) *position = ((uint64_t)request.position_hi << 32) | request.position_lo;
    return result;
}
rum_result_t rum_readdir(rum_handle_t handle, struct rum_directory_entry *entry)
{
    return rum_syscall3(RUM_SYS_READDIR, handle, (rum_address_t)(uintptr_t)entry, 0);
}
rum_result_t rum_chdir(const char *path) { return rum_syscall3(RUM_SYS_CHDIR, (rum_address_t)(uintptr_t)path, 0, 0); }
rum_result_t rum_getcwd(char *buffer, rum_size_t capacity) { return rum_syscall3(RUM_SYS_GETCWD, (rum_address_t)(uintptr_t)buffer, capacity, 0); }
rum_result_t rum_mkdir(const char *path) { return rum_syscall3(RUM_SYS_MKDIR, (rum_address_t)(uintptr_t)path, 0, 0); }
rum_result_t rum_remove(const char *path) { return rum_syscall3(RUM_SYS_REMOVE, (rum_address_t)(uintptr_t)path, 0, 0); }
rum_result_t rum_flush(rum_handle_t handle) { return rum_syscall3(RUM_SYS_FLUSH, handle, 0, 0); }
rum_result_t rum_run(const char *path, const struct rum_arguments *arguments, struct rum_process_result *result)
{
    struct rum_run_request request = { RUM_PROCESS_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)arguments,
        (rum_address_t)(uintptr_t)result, 0, {0, 0} };
    return rum_syscall3(RUM_SYS_RUN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_spawn(const char *path, const struct rum_arguments *arguments)
{
    struct rum_spawn_request request = { .version = RUM_LIFECYCLE_ABI_VERSION, .size = sizeof request,
        .path = (rum_address_t)(uintptr_t)path, .arguments = (rum_address_t)(uintptr_t)arguments };
    return rum_syscall3(RUM_SYS_SPAWN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_wait(rum_pid_t id, uint32_t flags, struct rum_process_result *result)
{
    struct rum_wait_request request = { .version = RUM_LIFECYCLE_ABI_VERSION, .size = sizeof request,
        .process_id = id, .result = (rum_address_t)(uintptr_t)result, .flags = flags };
    return rum_syscall3(RUM_SYS_WAIT, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_terminate(rum_pid_t id, uint32_t flags)
{
    struct rum_terminate_request request = { .version = RUM_LIFECYCLE_ABI_VERSION, .size = sizeof request,
        .process_id = id, .flags = flags };
    return rum_syscall3(RUM_SYS_TERMINATE, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_isatty(rum_handle_t handle) { return rum_syscall3(RUM_SYS_ISATTY, handle, 0, 0); }
static rum_result_t job_call(unsigned operation, uint32_t id)
{
    struct rum_job_request request = { .version = RUM_JOB_ABI_VERSION, .size = sizeof request,
        .operation = operation, .job_id = id };
    return rum_syscall3(RUM_SYS_JOB, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_job_commit(uint32_t id) { return job_call(RUM_JOB_COMMIT, id); }
rum_result_t rum_job_cancel(uint32_t id) { return job_call(RUM_JOB_CANCEL, id); }
rum_result_t rum_job_foreground(uint32_t id) { return job_call(RUM_JOB_FOCUS, id); }
rum_result_t rum_job_release(uint32_t id) { return job_call(RUM_JOB_RELEASE, id); }
rum_result_t rum_job_ack(uint32_t id) { return job_call(RUM_JOB_ACK, id); }
rum_result_t rum_job_create(const char *name, uint32_t flags)
{
    struct rum_job_request request = { .version = RUM_JOB_ABI_VERSION, .size = sizeof request,
        .operation = RUM_JOB_CREATE, .name = (rum_address_t)(uintptr_t)name, .flags = flags };
    return rum_syscall3(RUM_SYS_JOB, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_job_list(struct rum_job_entry *entries, uint32_t capacity, uint32_t *total)
{
    struct rum_job_request request = { .version = RUM_JOB_ABI_VERSION, .size = sizeof request,
        .operation = RUM_JOB_LIST, .entries = (rum_address_t)(uintptr_t)entries, .capacity = capacity };
    rum_result_t result = rum_syscall3(RUM_SYS_JOB, (rum_address_t)(uintptr_t)&request, 0, 0);
    if (result >= 0 && total) *total = request.total;
    return result;
}
rum_result_t rum_job_wait(uint32_t id, uint32_t flags, struct rum_job_entry *entry)
{
    struct rum_job_request request = { .version = RUM_JOB_ABI_VERSION, .size = sizeof request,
        .operation = RUM_JOB_WAIT, .job_id = id, .flags = flags,
        .entries = (rum_address_t)(uintptr_t)entry, .capacity = 1 };
    return rum_syscall3(RUM_SYS_JOB, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_yield(void) { return rum_syscall3(RUM_SYS_YIELD, 0, 0, 0); }
rum_result_t rum_process_list(struct rum_process_entry *entries, uint32_t capacity, uint32_t *total)
{
    struct rum_process_list_request request = { .version = RUM_PROCESS_LIST_VERSION, .size = sizeof request,
        .entries = (rum_address_t)(uintptr_t)entries, .capacity = capacity };
    rum_result_t result = rum_syscall3(RUM_SYS_PROCESS_LIST, (rum_address_t)(uintptr_t)&request, 0, 0);
    if (result >= 0 && total) *total = request.total;
    return result;
}
rum_result_t rum_sleep(uint32_t milliseconds)
{
    struct rum_sleep_request request = { .version = RUM_SLEEP_ABI_VERSION, .size = sizeof request,
        .milliseconds = milliseconds };
    return rum_syscall3(RUM_SYS_SLEEP, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_replace(const char *path, const void *data, rum_size_t bytes)
{
    struct rum_replace_request request = { RUM_FS_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)data, bytes, 0 };
    return rum_syscall3(RUM_SYS_REPLACE, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_console(uint32_t action) { return rum_syscall3(RUM_SYS_CONSOLE, action, 0, 0); }
rum_result_t rum_run_command(const char *path, const struct rum_arguments *arguments,
                             const char *text, struct rum_process_result *result)
{
    struct rum_command_request request = { RUM_COMMAND_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)arguments,
        (rum_address_t)(uintptr_t)result, RUM_RUN_COMMAND, (rum_address_t)(uintptr_t)text, 0 };
    return rum_syscall3(RUM_SYS_RUN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_command_text(char *text, rum_size_t capacity)
{
    return rum_syscall3(RUM_SYS_COMMAND_TEXT, (rum_address_t)(uintptr_t)text, capacity, 0);
}
rum_result_t rum_run_argv_command(const char *path, const struct rum_arguments *arguments, struct rum_process_result *result)
{
    struct rum_command_request request = { RUM_COMMAND_ARGV_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)arguments,
        (rum_address_t)(uintptr_t)result, RUM_RUN_COMMAND, 0, 0 };
    return rum_syscall3(RUM_SYS_RUN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_session(uint32_t action, const char *path)
{
    return rum_syscall3(RUM_SYS_SESSION, action, (rum_address_t)(uintptr_t)path, 0);
}

rum_result_t rum_read(rum_handle_t handle, void *buffer, rum_size_t capacity)
{
    return rum_syscall3(RUM_SYS_READ, handle, (rum_address_t)(uintptr_t)buffer, capacity);
}

rum_result_t rum_write(rum_handle_t handle, const void *buffer, rum_size_t bytes)
{
    return rum_syscall3(RUM_SYS_WRITE, handle, (rum_address_t)(uintptr_t)buffer, bytes);
}

rum_result_t rum_getpid(void)
{
    return rum_syscall3(RUM_SYS_GETPID, 0, 0, 0);
}

_Noreturn void rum_exit(int32_t status)
{
    (void)rum_syscall3(RUM_SYS_EXIT, (uint32_t)status, 0, 0);
    /* A returning exit violates the ABI. Fault in user mode rather than use
       a privileged halt instruction or silently continue past main. */
    for (;;) __asm__ volatile ("ud2");
}
