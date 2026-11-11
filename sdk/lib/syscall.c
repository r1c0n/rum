#include <rum/user.h>
rum_result_t rum_process_object_control(struct rum_process_object_request *request)
{
    return rum_syscall3(RUM_SYS_PROCESS_OBJECT, (rum_address_t)(uintptr_t)request, 0, 0);
}
rum_result_t rum_process_open(rum_pid_t pid, uint32_t rights, uint32_t flags, rum_object_handle_t *output)
{
    if (!output) return -RUM_EINVAL;
    struct rum_process_object_request request = { .version = RUM_PROCESS_OBJECT_VERSION, .size = sizeof request,
        .operation = RUM_PROCESS_OBJECT_OPEN, .process_id = pid, .rights = rights, .flags = flags };
    rum_result_t result = rum_process_object_control(&request);
    if (!result) *output = request.result_handle;
    return result;
}
static rum_result_t process_observe(rum_object_handle_t handle, unsigned action, unsigned flags,
                                    struct rum_process_object_information *output)
{
    struct rum_process_object_request request = { .version = RUM_PROCESS_OBJECT_VERSION, .size = sizeof request,
        .operation = action, .handle = handle, .flags = flags, .data = (rum_address_t)(uintptr_t)output };
    return rum_process_object_control(&request);
}
rum_result_t rum_process_query(rum_object_handle_t handle, struct rum_process_object_information *output)
{
    return process_observe(handle, RUM_PROCESS_OBJECT_QUERY, 0, output);
}
rum_result_t rum_process_wait(rum_object_handle_t handle, uint32_t flags, struct rum_process_object_information *output)
{
    return process_observe(handle, RUM_PROCESS_OBJECT_WAIT, flags, output);
}
rum_result_t rum_process_terminate(rum_object_handle_t handle)
{
    return process_observe(handle, RUM_PROCESS_OBJECT_TERMINATE, 0, NULL);
}
rum_result_t rum_object_control(struct rum_object_request *request)
{
    return rum_syscall3(RUM_SYS_OBJECT, (rum_address_t)(uintptr_t)request, 0, 0);
}
rum_result_t rum_object_query(rum_object_handle_t handle, uint32_t type, struct rum_object_information *output)
{
    if (!output) return -RUM_EINVAL;
    struct rum_object_request request = { .version = RUM_OBJECT_ABI_VERSION, .size = sizeof request,
        .operation = RUM_OBJECT_QUERY, .handle = handle, .type = type };
    rum_result_t result = rum_object_control(&request);
    if (!result) *output = (struct rum_object_information){ .version = RUM_OBJECT_ABI_VERSION,
        .size = sizeof *output, .type = request.type, .rights = request.rights, .flags = request.flags };
    return result;
}
rum_result_t rum_object_duplicate(rum_object_handle_t handle, uint32_t type, uint32_t rights,
                                   uint32_t flags, rum_object_handle_t *output)
{
    if (!output) return -RUM_EINVAL;
    struct rum_object_request request = { .version = RUM_OBJECT_ABI_VERSION, .size = sizeof request,
        .operation = RUM_OBJECT_DUPLICATE, .handle = handle, .type = type, .rights = rights, .flags = flags };
    rum_result_t result = rum_object_control(&request);
    if (!result) *output = request.result_handle;
    return result;
}
rum_result_t rum_object_close(rum_object_handle_t handle)
{
    struct rum_object_request request = { .version = RUM_OBJECT_ABI_VERSION, .size = sizeof request,
        .operation = RUM_OBJECT_CLOSE, .handle = handle };
    return rum_object_control(&request);
}
rum_result_t rum_object_from_descriptor(rum_handle_t descriptor, uint32_t type, uint32_t rights,
                                       uint32_t flags, rum_object_handle_t *output)
{
    if (!output) return -RUM_EINVAL;
    struct rum_object_request request = { .version = RUM_OBJECT_ABI_VERSION, .size = sizeof request,
        .operation = RUM_OBJECT_FROM_DESCRIPTOR, .handle = descriptor, .type = type, .rights = rights, .flags = flags };
    rum_result_t result = rum_object_control(&request);
    if (!result) *output = request.result_handle;
    return result;
}
rum_result_t rum_object_io(struct rum_object_io_request *request)
{
    return rum_syscall3(RUM_SYS_OBJECT_IO, (rum_address_t)(uintptr_t)request, 0, 0);
}
static struct rum_object_io_request io_packet(uint32_t operation, rum_object_handle_t handle)
{
    return (struct rum_object_io_request){ .version = RUM_OBJECT_IO_VERSION,
        .size = sizeof(struct rum_object_io_request), .operation = operation, .handle = handle };
}
rum_result_t rum_object_read(rum_object_handle_t handle, void *data, rum_size_t bytes)
{
    struct rum_object_io_request request = io_packet(RUM_OBJECT_IO_READ, handle);
    request.data = (rum_address_t)(uintptr_t)data; request.bytes = bytes;
    return rum_object_io(&request);
}
rum_result_t rum_object_write(rum_object_handle_t handle, const void *data, rum_size_t bytes)
{
    struct rum_object_io_request request = io_packet(RUM_OBJECT_IO_WRITE, handle);
    request.data = (rum_address_t)(uintptr_t)data; request.bytes = bytes;
    return rum_object_io(&request);
}
rum_result_t rum_object_seek(rum_object_handle_t handle, int64_t displacement, uint32_t whence, uint64_t *position)
{
    struct rum_object_io_request request = io_packet(RUM_OBJECT_IO_SEEK, handle);
    request.type = RUM_OBJECT_FILE; request.whence = whence;
    request.offset_lo = (uint32_t)(uint64_t)displacement; request.offset_hi = (uint32_t)((uint64_t)displacement >> 32);
    rum_result_t result = rum_object_io(&request);
    if (!result && position) *position = (uint64_t)request.offset_hi << 32 | request.offset_lo;
    return result;
}
rum_result_t rum_object_readdir(rum_object_handle_t handle, struct rum_directory_entry *entry)
{
    struct rum_object_io_request request = io_packet(RUM_OBJECT_IO_READDIR, handle);
    request.type = RUM_OBJECT_DIRECTORY; request.data = (rum_address_t)(uintptr_t)entry;
    return rum_object_io(&request);
}
rum_result_t rum_object_flush(rum_object_handle_t handle)
{
    struct rum_object_io_request request = io_packet(RUM_OBJECT_IO_FLUSH, handle);
    return rum_object_io(&request);
}
rum_result_t rum_object_isatty(rum_object_handle_t handle)
{
    struct rum_object_io_request request = io_packet(RUM_OBJECT_IO_ISATTY, handle);
    return rum_object_io(&request);
}
rum_result_t rum_clock(struct rum_clock_request *request)
{
    return rum_syscall3(RUM_SYS_CLOCK, (rum_address_t)(uintptr_t)request, 0, 0);
}
rum_result_t rum_monotonic_ms(uint64_t *milliseconds)
{
    if (!milliseconds) return -RUM_EINVAL;
    struct rum_clock_request request = { .version = RUM_CLOCK_ABI_VERSION, .size = sizeof request };
    rum_result_t result = rum_clock(&request);
    if (result < 0) return result;
    if (!request.tick_hz) return -RUM_EIO;
    uint64_t ticks = (uint64_t)request.ticks_hi << 32 | request.ticks_lo;
    uint64_t seconds = ticks / request.tick_hz;
    if (seconds > (UINT64_MAX - 999) / 1000) return -RUM_EOVERFLOW;
    *milliseconds = seconds * 1000 + (ticks % request.tick_hz) * 1000 / request.tick_hz;
    return 0;
}
rum_result_t rum_vm_protect(rum_address_t address, rum_size_t bytes, uint32_t protection)
{
    struct rum_vm_protect_request request = { .version = RUM_VM_ABI_VERSION, .size = sizeof request,
        .address = address, .bytes = bytes, .protection = protection };
    return rum_syscall3(RUM_SYS_VM_PROTECT, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_vm_allocate(struct rum_vm_allocate_request *request)
{
    return rum_syscall3(RUM_SYS_VM_ALLOCATE, (rum_address_t)(uintptr_t)request, 0, 0);
}
rum_result_t rum_vm_release(rum_address_t address, rum_size_t bytes)
{
    struct rum_vm_release_request request = { .version = RUM_VM_ABI_VERSION, .size = sizeof request,
        .address = address, .bytes = bytes };
    return rum_syscall3(RUM_SYS_VM_RELEASE, (rum_address_t)(uintptr_t)&request, 0, 0);
}
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
rum_result_t rum_terminal_styled_line(const struct rum_terminal_cell *cells, uint32_t count, uint32_t cursor)
{
    struct rum_terminal_request request = { .operation = RUM_TERM_STYLED_LINE, .count = count, .x = cursor,
        .data = (rum_address_t)(uintptr_t)cells }; return terminal_call(&request);
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

rum_result_t rum_spawn_with_objects(const char *path, const struct rum_arguments *arguments,
                                   const struct rum_handle_mapping *mappings, uint32_t count, uint32_t flags,
                                   const struct rum_object_inheritance *objects, uint32_t object_count)
{
    struct rum_spawn_objects_request request = { .streams = { RUM_SPAWN_OBJECTS_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)arguments,
        (rum_address_t)(uintptr_t)mappings, count, flags, 0 },
        .objects = (rum_address_t)(uintptr_t)objects, .object_count = object_count };
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
