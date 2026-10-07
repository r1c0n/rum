#include <rum/abi/error.h>
#include <rum/abi/console.h>
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/keyboard.h>
#include <rum/memory.h>
#include <rum/memory_layout.h>
#include <rum/paging.h>
#include <rum/process.h>
#include <rum/serial.h>
#include <rum/snake.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>

#define CONSOLE_CHUNK 128u
#define FILE_CHUNK 512u

static bool accessible(struct paging_space *space, uint32_t address, uint32_t bytes, bool write)
{
    return !bytes || (memory_range_contains(RUM_USER_BASE, RUM_USER_END, address, bytes) &&
                     paging_user_accessible(space, address, bytes, write));
}

static rum_result_t copy_path(struct paging_space *space, uint32_t address, char *path)
{
    for (unsigned i = 0; i < RUM_ABI_PATH_CAPACITY; ++i) {
        if (i > UINT32_MAX - address || !accessible(space, address + i, 1, false) ||
            !paging_copy_from_user(space, path + i, address + i, 1)) return -RUM_EFAULT;
        if (!path[i]) return i ? 0 : -RUM_EINVAL;
    }
    return -RUM_ENAMETOOLONG;
}

static rum_result_t stream_read(struct paging_space *space, uint32_t address, uint32_t chunk)
{
    char buffer[CONSOLE_CHUNK];
    for (;;) {
        task_cancel_current_if_requested();
        struct task_event *event = keyboard_input_event();
        uint32_t observed = task_event_sequence(event), count = 0;
        while (count < chunk && keyboard_read(&buffer[count])) ++count;
        if (count) {
            if (!paging_copy_to_user(space, address, buffer, count)) return -RUM_EFAULT;
            return (rum_result_t)count;
        }
        if (!task_wait(event, observed)) return -RUM_EIO;
    }
}

static rum_result_t transfer(struct paging_space *space, struct process_handles *table,
                             rum_handle_t id, uint32_t address, uint32_t bytes, bool write)
{
    struct process_handle *handle = process_handle_get(table, id);
    if (!handle || !(handle->access & (write ? FS_WRITE : FS_READ))) return -RUM_EBADF;
    if (handle->kind == HANDLE_DIRECTORY) return -RUM_EISDIR;
    if (!bytes) return 0;
    /* Validate the entire request, even though this call copies one chunk. */
    if (!accessible(space, address, bytes, !write)) return -RUM_EFAULT;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (handle->kind == HANDLE_INPUT) return stream_read(space, address, bytes < CONSOLE_CHUNK ? bytes : CONSOLE_CHUNK);
    uint32_t limit = handle->kind == HANDLE_OUTPUT ? CONSOLE_CHUNK : FILE_CHUNK;
    uint32_t chunk = bytes < limit ? bytes : limit;
    char buffer[FILE_CHUNK];
    if (write && !paging_copy_from_user(space, buffer, address, chunk)) return -RUM_EFAULT;
    if (handle->kind == HANDLE_OUTPUT) {
        terminal_write(buffer, chunk);
        for (unsigned i = 0; i < chunk; ++i) {
            if (buffer[i] == '\n') serial_putchar('\r');
            serial_putchar(buffer[i]);
        }
        return (rum_result_t)chunk;
    }
    if (bytes > UINT64_MAX - handle->offset) return -RUM_EOVERFLOW;
    rum_result_t result = write ? process_handle_write(handle, buffer, chunk) : process_handle_read(handle, buffer, chunk);
    if (!write && result > 0 && !paging_copy_to_user(space, address, buffer, (size_t)result)) cpu_halt();
    return result;
}

static rum_result_t open_file(struct paging_space *space, struct process_handles *table, uint32_t address)
{
    struct rum_open_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_FS_ABI_VERSION || request.size != sizeof request || request.reserved ||
        !request.access || (request.access & ~(RUM_OPEN_READ | RUM_OPEN_WRITE)) ||
        (request.flags & ~RUM_OPEN_DIRECTORY) ||
        ((request.flags & RUM_OPEN_DIRECTORY) && request.access != RUM_OPEN_READ)) return -RUM_EINVAL;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.path, path);
    return error ? error : process_handle_open(table, task_current_filesystem(), path, request.access, request.flags);
}

static rum_result_t seek_file(struct paging_space *space, struct process_handles *table, uint32_t id, uint32_t address)
{
    struct process_handle *handle = process_handle_get(table, id);
    if (!handle) return -RUM_EBADF;
    struct rum_seek_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_FS_ABI_VERSION || request.size != sizeof request || request.reserved ||
        request.position_lo || request.position_hi || request.whence > RUM_SEEK_END) return -RUM_EINVAL;
    uint64_t position = 0;
    rum_result_t error = process_handle_seek(handle, ((uint64_t)request.offset_hi << 32) | request.offset_lo,
                                            request.whence, &position);
    if (error) return error;
    request.position_lo = (uint32_t)position; request.position_hi = (uint32_t)(position >> 32);
    if (!paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    return 0;
}

static rum_result_t list_directory(struct paging_space *space, struct process_handles *table, uint32_t id, uint32_t address)
{
    struct process_handle *handle = process_handle_get(table, id);
    if (!handle) return -RUM_EBADF;
    struct rum_directory_entry output;
    if (!accessible(space, address, sizeof output, true) ||
        !paging_copy_from_user(space, &output, address, sizeof output)) return -RUM_EFAULT;
    if (output.version != RUM_FS_ABI_VERSION || output.size != sizeof output || output.reserved) return -RUM_EINVAL;
    output = (struct rum_directory_entry){ .version = RUM_FS_ABI_VERSION, .size = sizeof output };
    struct fs_entry entry;
    rum_result_t result = process_handle_readdir(handle, &entry);
    if (result < 0) return result;
    if (result) {
        output.kind = entry.kind == FS_FILE ? RUM_ENTRY_FILE : RUM_ENTRY_DIRECTORY;
        output.size_lo = (uint32_t)entry.size; output.size_hi = (uint32_t)(entry.size >> 32);
        memcpy(output.name, entry.name, sizeof output.name);
    }
    if (!paging_copy_to_user(space, address, &output, sizeof output)) cpu_halt();
    return result;
}

static rum_result_t path_operation(struct paging_space *space, uint32_t address, uint32_t number)
{
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, address, path);
    if (error) return error;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    struct fs_context *cwd = task_current_filesystem();
    enum fs_error result = number == RUM_SYS_CHDIR ? fs_context_chdir(cwd, path) :
                           number == RUM_SYS_MKDIR ? fs_mkdir(cwd, path) : fs_remove(cwd, path);
    return process_fs_error(result);
}

static rum_result_t working_directory(struct paging_space *space, uint32_t address, uint32_t capacity)
{
    if (!capacity) return -RUM_ERANGE;
    if (!accessible(space, address, capacity, true)) return -RUM_EFAULT;
    const struct fs_context *cwd = task_current_filesystem();
    if (!cwd) return -RUM_EIO;
    size_t bytes = 1;
    while (bytes < FS_PATH_CAPACITY && cwd->path[bytes - 1]) ++bytes;
    if (bytes > capacity) return -RUM_ERANGE;
    if (!paging_copy_to_user(space, address, cwd->path, bytes)) return -RUM_EFAULT;
    return (rum_result_t)bytes;
}

static rum_result_t run_child(struct paging_space *space, uint32_t address)
{
    struct rum_run_request request;
    struct rum_process_result output;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    bool command = request.version == RUM_COMMAND_ABI_VERSION;
    if (request.size != sizeof request || request.reserved[1] ||
        (command ? request.flags != RUM_RUN_COMMAND :
         request.version != RUM_PROCESS_ABI_VERSION || request.flags || request.reserved[0])) return -RUM_EINVAL;
    if (command && !task_current_is_shell()) return -RUM_EACCES;
    if (!accessible(space, request.result, sizeof output, true) ||
        !paging_copy_from_user(space, &output, request.result, sizeof output)) return -RUM_EFAULT;
    if (output.version != RUM_PROCESS_ABI_VERSION || output.size != sizeof output || output.reserved)
        return -RUM_EINVAL;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.path, path);
    if (error) return error;
    char text[RUM_COMMAND_TEXT_CAPACITY];
    if (command) {
        /* Empty tails are valid; every byte must be printable shell input. */
        unsigned i;
        for (i = 0; i < sizeof text; ++i) {
            if (i > UINT32_MAX - request.reserved[0] ||
                !accessible(space, request.reserved[0] + i, 1, false) ||
                !paging_copy_from_user(space, text + i, request.reserved[0] + i, 1)) return -RUM_EFAULT;
            if (!text[i]) break;
            if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126) return -RUM_EINVAL;
        }
        if (i == sizeof text) return -RUM_E2BIG;
    }
    if (!accessible(space, request.arguments, sizeof(struct rum_arguments), false)) return -RUM_EFAULT;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    /* This packet is larger than a page. Keep it off the guarded kernel stack. */
    struct rum_arguments *arguments = kmalloc(sizeof *arguments);
    if (!arguments) return -RUM_ENOMEM;
    if (!paging_copy_from_user(space, arguments, request.arguments, sizeof *arguments)) error = -RUM_EFAULT;
    else if (!elf_arguments_valid(arguments)) error = -RUM_EINVAL;
    struct process_result result;
    if (!error) error = command ? process_run_command(path, arguments, text, &result) :
                                process_run_foreground(path, arguments, &result);
    (void)kfree(arguments);
    if (error) return error;
    output = (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION, .size = sizeof output,
        .process_id = result.process_id, .termination = result.termination,
        .status = result.exit_status, .fault_vector = result.termination == TASK_TERMINATION_FAULT ? result.fault.vector : 0 };
    if (!paging_copy_to_user(space, request.result, &output, sizeof output)) cpu_halt();
    return 0;
}

static rum_result_t command_text(struct paging_space *space, uint32_t address, uint32_t capacity, uint32_t reserved)
{
    if (reserved) return -RUM_EINVAL;
    const char *text = task_current_command_text();
    if (!text) return -RUM_EACCES;
    if (!capacity) return -RUM_ERANGE;
    if (!accessible(space, address, capacity, true)) return -RUM_EFAULT;
    unsigned bytes = 1;
    while (text[bytes - 1]) ++bytes;
    if (bytes > capacity) return -RUM_ERANGE;
    return paging_copy_to_user(space, address, text, bytes) ? (rum_result_t)bytes : -RUM_EFAULT;
}

static rum_result_t session_control(struct paging_space *space, uint32_t action, uint32_t address, uint32_t reserved)
{
    if (reserved || action < RUM_SESSION_CHDIR || action > RUM_SESSION_RECOVERY ||
        (action != RUM_SESSION_CHDIR && address)) return -RUM_EINVAL;
    char path[RUM_ABI_PATH_CAPACITY];
    if (action == RUM_SESSION_CHDIR) {
        rum_result_t error = copy_path(space, address, path);
        if (error) return error;
    }
    return task_session_control(action, action == RUM_SESSION_CHDIR ? path : NULL);
}

static rum_result_t replace_file(struct paging_space *space, uint32_t address)
{
    struct rum_replace_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_FS_ABI_VERSION || request.size != sizeof request || request.reserved)
        return -RUM_EINVAL;
    if (request.bytes > RUM_ABI_REPLACE_LIMIT) return -RUM_E2BIG;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.path, path);
    if (error) return error;
    if (!accessible(space, request.data, request.bytes, false)) return -RUM_EFAULT;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    void *data = request.bytes ? kmalloc(request.bytes) : NULL;
    if (request.bytes && !data) return -RUM_ENOMEM;
    if (request.bytes && !paging_copy_from_user(space, data, request.data, request.bytes)) error = -RUM_EFAULT;
    else error = process_fs_error(fs_replace(task_current_filesystem(), path, data, request.bytes));
    (void)kfree(data);
    return error;
}

static rum_result_t console_action(uint32_t action, uint32_t reserved1, uint32_t reserved2)
{
    if (reserved1 || reserved2 || action > RUM_CONSOLE_SNAKE) return -RUM_EINVAL;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (action == RUM_CONSOLE_CLEAR) {
        terminal_clear(); serial_writestring("\x1b[2J\x1b[H"); return 0;
    }
    if (!snake_start(timer_ticks())) return -RUM_ENOMEM;
    while (snake_active()) {
        uint32_t observed = task_event_sequence(task_work_event());
        if (task_current_cancelled()) {
            snake_receive('q', timer_ticks());
            task_cancel_current_if_requested();
        }
        char key;
        while (keyboard_read(&key)) snake_receive(key == '\x03' ? 'q' : key, timer_ticks());
        snake_tick(timer_ticks());
        if (snake_active()) (void)task_wait(task_work_event(), observed);
    }
    return 0;
}

void syscall_dispatch(struct exception_frame *frame)
{
    if (!frame || frame->vector != RUM_SYSCALL_VECTOR || frame->error ||
        !exception_frame_from_user(frame) || !task_current_is_process() || irq_in_handler()) {
        if (frame) exception_dispatch(frame);
        cpu_halt();
    }
    /* The gate protects frame construction. No FS callback runs with IF clear. */
    cpu_interrupt_enable();
    task_cancel_current_if_requested();
    struct paging_space *space = task_current_process_space();
    struct process_handles *handles = task_current_handles();
    rum_result_t result;
    switch (frame->eax) {
    case RUM_SYS_EXIT: task_exit_with_status((rum_result_t)frame->ebx);
    case RUM_SYS_GETPID: result = (rum_result_t)task_current_process_id(); break;
    case RUM_SYS_READ: case RUM_SYS_WRITE:
        result = transfer(space, handles, frame->ebx, frame->ecx, frame->edx, frame->eax == RUM_SYS_WRITE); break;
    case RUM_SYS_OPEN: result = open_file(space, handles, frame->ebx); break;
    case RUM_SYS_CLOSE: result = process_handle_close(handles, frame->ebx); break;
    case RUM_SYS_SEEK: result = seek_file(space, handles, frame->ebx, frame->ecx); break;
    case RUM_SYS_READDIR: result = list_directory(space, handles, frame->ebx, frame->ecx); break;
    case RUM_SYS_CHDIR: case RUM_SYS_MKDIR: case RUM_SYS_REMOVE:
        result = path_operation(space, frame->ebx, frame->eax); break;
    case RUM_SYS_GETCWD: result = working_directory(space, frame->ebx, frame->ecx); break;
    case RUM_SYS_FLUSH: result = process_handle_flush(process_handle_get(handles, frame->ebx)); break;
    case RUM_SYS_RUN: result = run_child(space, frame->ebx); break;
    case RUM_SYS_REPLACE: result = replace_file(space, frame->ebx); break;
    case RUM_SYS_CONSOLE: result = console_action(frame->ebx, frame->ecx, frame->edx); break;
    case RUM_SYS_COMMAND_TEXT: result = command_text(space, frame->ebx, frame->ecx, frame->edx); break;
    case RUM_SYS_SESSION: result = session_control(space, frame->ebx, frame->ecx, frame->edx); break;
    default: result = -RUM_ENOSYS; break;
    }
    frame->eax = (uint32_t)result;
    task_exit_shell_if_requested();
}
