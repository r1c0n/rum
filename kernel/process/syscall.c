#include <rum/abi/error.h>
#include <rum/abi/console.h>
#include <rum/block.h>
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
#include <rum/pipe.h>
#include <rum/process.h>
#include <rum/serial.h>
#include <rum/terminal_session.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/abi/job.h>
#include <rum/abi/process-object.h>
#include <rum/terminal.h>
#include <rum/timer.h>

#define CONSOLE_CHUNK 128u
#define FILE_CHUNK 512u
_Static_assert(FILE_CHUNK == RUM_PIPE_ATOMIC_WRITE, "pipe syscall chunks retain atomic-write semantics");

static bool accessible(struct paging_space *space, uint32_t address, uint32_t bytes, bool write)
{
    return !bytes || (memory_range_contains(RUM_USER_BASE, RUM_USER_END, address, bytes) &&
                     paging_user_accessible(space, address, bytes, write));
}

static rum_result_t memory_region(struct paging_space *space, uint32_t address, uint32_t operation,
                                  uint32_t user_stack)
{
    /* One user thread currently owns each space. Keep the packet snapshot,
       mapping transaction and output publication inside one non-switching
       scope. No filesystem callbacks, waits or temporary heap allocations. */
    if (fs_operation_active() || block_request_active() || !task_switch_hold()) return -RUM_EBUSY;
    rum_result_t result;
    if (operation == RUM_SYS_VM_ALLOCATE) {
        struct rum_vm_allocate_request request;
        if (!accessible(space, address, sizeof request, true) ||
            !paging_copy_from_user(space, &request, address, sizeof request)) result = -RUM_EFAULT;
        else {
            result = paging_region_allocate(space, &request);
            /* Allocation cannot replace the existing mapping containing the
               validated packet, so this copy cannot lose its writable pages. */
            if (!result && !paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
        }
    } else if (operation == RUM_SYS_VM_PROTECT) {
        struct rum_vm_protect_request request;
        if (!accessible(space, address, sizeof request, false) ||
            !paging_copy_from_user(space, &request, address, sizeof request)) result = -RUM_EFAULT;
        else result = paging_region_protect(space, &request, user_stack);
        /* The copied packet may become read-only with its region. No copyout. */
    } else {
        struct rum_vm_release_request request;
        if (!accessible(space, address, sizeof request, false) ||
            !paging_copy_from_user(space, &request, address, sizeof request)) result = -RUM_EFAULT;
        else result = paging_region_release(space, &request, user_stack);
        /* The packet may itself have occupied the released region. It has
           already been copied; release has no user-memory output. */
    }
    if (!task_switch_release()) cpu_halt();
    return result;
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

static rum_result_t retain_io(struct kernel_object *object)
{
    if (task_io_begin(object)) return 0;
    if (object_release(object, OBJECT_OPERATION)) cpu_halt();
    return -RUM_EBUSY;
}
static rum_result_t retain_descriptor(struct process_handles *table, rum_handle_t id, struct process_handle **handle)
{
    rum_result_t error = process_handle_acquire(table, id, handle);
    return error ? error : retain_io(&(*handle)->object);
}
static rum_result_t stream_read(struct paging_space *space, uint32_t address, uint32_t chunk)
{
    char buffer[CONSOLE_CHUNK];
    for (;;) {
        task_cancel_current_if_requested();
        if (!task_current_owns_console()) return -RUM_EACCES;
        struct task_event *event = task_shell_input_event();
        if (!event) event = keyboard_input_event();
        uint32_t observed = task_event_sequence(event), count = 0;
        if (task_shell_has_completion()) return -RUM_EINTR;
        while (count < chunk && keyboard_read(&buffer[count])) ++count;
        if (count) {
            if (!paging_copy_to_user(space, address, buffer, count)) return -RUM_EFAULT;
            return (rum_result_t)count;
        }
        if (!task_wait(event, observed)) return -RUM_EIO;
    }
}

static rum_result_t transfer_handle(struct paging_space *space, struct process_handle *handle,
                                    uint32_t address, uint32_t bytes, bool write)
{
    if (!handle || !(handle->access & (write ? FS_WRITE : FS_READ))) return -RUM_EBADF;
    if (write && handle->object.type->authorize) {
        rum_result_t error = handle->object.type->authorize(&handle->object, RUM_RIGHT_WRITE);
        if (error) return error;
    }
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
        if (!task_switch_hold()) return -RUM_EBUSY;
        if (terminal_session_output(task_current_id())) terminal_write(buffer, chunk);
        for (unsigned i = 0; i < chunk; ++i) {
            if (buffer[i] == '\n') serial_putchar('\r');
            serial_putchar(buffer[i]);
        }
        if (!task_switch_release()) cpu_halt();
        return (rum_result_t)chunk;
    }
    if (!handle->pipe && !(write && handle->append) && bytes > UINT64_MAX - handle->offset) return -RUM_EOVERFLOW;
    rum_result_t result = write ? process_handle_write(handle, buffer, chunk) : process_handle_read(handle, buffer, chunk);
    if (!write && result > 0 && !paging_copy_to_user(space, address, buffer, (size_t)result)) cpu_halt();
    return result;
}
static rum_result_t transfer(struct paging_space *space, struct process_handles *table,
                             rum_handle_t id, uint32_t address, uint32_t bytes, bool write)
{
    struct process_handle *handle;
    rum_result_t result = retain_descriptor(table, id, &handle);
    return result ? result : transfer_handle(space, handle, address, bytes, write);
}

static rum_result_t open_file(struct paging_space *space, struct process_handles *table, uint32_t address)
{
    struct rum_open_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_FS_ABI_VERSION || request.size != sizeof request || request.reserved ||
        !request.access || (request.access & ~(RUM_OPEN_READ | RUM_OPEN_WRITE)) ||
        (request.flags & ~(RUM_OPEN_DIRECTORY | RUM_OPEN_CREATE | RUM_OPEN_TRUNCATE | RUM_OPEN_APPEND | RUM_OPEN_EXCLUSIVE)) ||
        ((request.flags & RUM_OPEN_EXCLUSIVE) && !(request.flags & RUM_OPEN_CREATE)) ||
        ((request.flags & RUM_OPEN_DIRECTORY) && (request.access != RUM_OPEN_READ || request.flags != RUM_OPEN_DIRECTORY)) ||
        ((request.flags & ~RUM_OPEN_DIRECTORY) && !(request.access & RUM_OPEN_WRITE))) return -RUM_EINVAL;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.path, path);
    return error ? error : process_handle_open(table, task_current_filesystem(), path, request.access, request.flags);
}

static rum_result_t file_information(struct paging_space *space, struct process_handles *table, uint32_t address)
{
    struct rum_stat_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_METADATA_ABI_VERSION || request.size != sizeof request ||
        request.flags > RUM_STAT_HANDLE || request.reserved[0] || request.reserved[1] ||
        (request.flags ? request.path != 0 : request.handle != 0)) return -RUM_EINVAL;
    if (!accessible(space, request.data, sizeof(struct rum_file_information), true) ||
        ((uint64_t)request.data < (uint64_t)address + sizeof request &&
         (uint64_t)address < (uint64_t)request.data + sizeof(struct rum_file_information))) return -RUM_EFAULT;
    struct process_handle *handle = NULL; char path[RUM_ABI_PATH_CAPACITY];
    if (request.flags) {
        rum_result_t error = retain_descriptor(table, request.handle, &handle);
        if (error) return error;
        if (handle->kind != HANDLE_FILE && handle->kind != HANDLE_DIRECTORY) return -RUM_EBADF;
    } else {
        rum_result_t error = copy_path(space, request.path, path);
        if (error) return error;
    }
    if (!process_filesystem_context()) return -RUM_EBUSY;
    struct fs_information info;
    enum fs_error error = handle ? fs_stat(handle->reference, &info) : fs_stat_path(task_current_filesystem(), path, &info);
    if (error != FS_OK) return process_fs_error(error);
    struct rum_file_information output = {
        .version = RUM_METADATA_ABI_VERSION, .size = sizeof output,
        .kind = info.kind == FS_FILE ? RUM_ENTRY_FILE : RUM_ENTRY_DIRECTORY,
        .flags = info.read_only_volume ? RUM_INFO_READ_ONLY_VOLUME : 0,
        .size_lo = (uint32_t)info.size, .size_hi = (uint32_t)(info.size >> 32),
        .volume = (uint32_t)info.identity.mount + 1, .generation = info.identity.generation,
        .object_lo = (uint32_t)info.identity.object, .object_hi = (uint32_t)(info.identity.object >> 32)
    };
    if (!paging_copy_to_user(space, request.data, &output, sizeof output)) cpu_halt();
    return 0;
}
static rum_result_t rename_file(struct paging_space *space, uint32_t address)
{
    struct rum_rename_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_METADATA_ABI_VERSION || request.size != sizeof request ||
        (request.flags & ~RUM_RENAME_REPLACE) || request.reserved) return -RUM_EINVAL;
    char source[RUM_ABI_PATH_CAPACITY], destination[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.source, source);
    if (!error) error = copy_path(space, request.destination, destination);
    if (error) return error;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    return process_fs_error(fs_rename(task_current_filesystem(), source, destination, request.flags != 0));
}
static rum_result_t create_pipe(struct paging_space *space, struct process_handles *table, uint32_t address)
{
    struct rum_pipe_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_PIPE_ABI_VERSION || request.size != sizeof request || request.flags ||
        request.reserved || request.read_handle || request.write_handle || request.capacity || request.atomic_write)
        return -RUM_EINVAL;
    rum_result_t error = process_handles_pipe(table, &request.read_handle, &request.write_handle);
    if (error) return error;
    request.capacity = RUM_PIPE_CAPACITY; request.atomic_write = RUM_PIPE_ATOMIC_WRITE;
    if (!paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    return 0;
}

static rum_result_t duplicate_handle(struct paging_space *space, struct process_handles *table, uint32_t address)
{
    struct rum_duplicate_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_HANDLE_ABI_VERSION || request.size != sizeof request || request.reserved ||
        (request.flags & ~RUM_DUP_CLOSPAWN)) return -RUM_EINVAL;
    return process_handle_duplicate(table, request.source, request.target, request.flags);
}

static rum_result_t seek_file(struct paging_space *space, struct process_handles *table, uint32_t id, uint32_t address)
{
    struct process_handle *handle;
    rum_result_t retained = retain_descriptor(table, id, &handle);
    if (retained) return retained;
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

static rum_result_t list_directory_handle(struct paging_space *space, struct process_handle *handle, uint32_t address)
{
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
static rum_result_t list_directory(struct paging_space *space, struct process_handles *table, uint32_t id, uint32_t address)
{
    struct process_handle *handle;
    rum_result_t result = retain_descriptor(table, id, &handle);
    return result ? result : list_directory_handle(space, handle, address);
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
    bool text_command = request.version == RUM_COMMAND_ABI_VERSION;
    bool argv_command = request.version == RUM_COMMAND_ARGV_VERSION;
    bool command = text_command || argv_command;
    if (request.size != sizeof request || request.reserved[1] ||
        (argv_command && request.reserved[0]) || (command ? request.flags != RUM_RUN_COMMAND :
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
    if (text_command) {
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
    if (!error) error = argv_command ? process_run_argv_command(path, arguments, &result) :
                                text_command ? process_run_command(path, arguments, text, &result) :
                                process_run_foreground(path, arguments, &result);
    (void)kfree(arguments);
    if (error) return error;
    output = (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION, .size = sizeof output,
        .process_id = result.process_id, .termination = result.termination,
        .status = result.exit_status, .fault_vector = result.termination == TASK_TERMINATION_FAULT ? result.fault.vector : 0 };
    if (!paging_copy_to_user(space, request.result, &output, sizeof output)) cpu_halt();
    return 0;
}

static rum_result_t spawn_child(struct paging_space *space, uint32_t address)
{
    struct rum_spawn_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_LIFECYCLE_ABI_VERSION || request.size != sizeof request ||
        request.flags || request.reserved[0] || request.reserved[1] || request.reserved[2]) return -RUM_EINVAL;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.path, path);
    if (error) return error;
    if (!accessible(space, request.arguments, sizeof(struct rum_arguments), false)) return -RUM_EFAULT;
    struct rum_arguments *arguments = kmalloc(sizeof *arguments);
    if (!arguments) return -RUM_ENOMEM;
    if (!paging_copy_from_user(space, arguments, request.arguments, sizeof *arguments)) error = -RUM_EFAULT;
    else if (!elf_arguments_valid(arguments)) error = -RUM_EINVAL;
    task_id child = 0;
    if (!error) error = process_spawn(path, arguments, &child);
    (void)kfree(arguments);
    if (error) return error;
    struct task_information info;
    if (!task_query(child, &info)) cpu_halt();
    return (rum_result_t)info.process_id;
}

static rum_result_t spawn_mapped_child(struct paging_space *space, uint32_t address)
{
    /* Validate/copy the complete legacy packet first, preserving its EFAULT
       precedence for truncated ranges. Version 2 then adds a bounded suffix. */
    struct rum_spawn_objects_request packet = {0};
    if (!accessible(space, address, sizeof packet.streams, false) ||
        !paging_copy_from_user(space, &packet.streams, address, sizeof packet.streams)) return -RUM_EFAULT;
    uint32_t version = packet.streams.version;
    bool explicit_objects = version == RUM_SPAWN_OBJECTS_VERSION;
    uint32_t size = explicit_objects ? sizeof(struct rum_spawn_objects_request) : sizeof(struct rum_spawn_mapped_request);
    if ((!explicit_objects && version != RUM_SPAWN_MAPPED_VERSION) || packet.streams.size != size) return -RUM_EINVAL;
    if (explicit_objects && (!accessible(space, address, size, false) ||
        !paging_copy_from_user(space, &packet, address, size))) return -RUM_EFAULT;
    struct rum_spawn_mapped_request request = packet.streams;
    if (request.version != version || request.size != size || request.reserved ||
        (request.flags & ~(RUM_SPAWN_NO_DEFAULT_STREAMS | RUM_SPAWN_PIPELINE | RUM_SPAWN_JOB)) ||
        ((request.flags & RUM_SPAWN_JOB) && (request.flags & RUM_SPAWN_PIPELINE)) || request.mapping_count > RUM_SPAWN_MAPPING_LIMIT ||
        (!request.mapping_count && request.mappings)) return -RUM_EINVAL;
    if (packet.object_count > RUM_OBJECT_HANDLE_LIMIT || (!packet.object_count && packet.objects)) return -RUM_EINVAL;
    for (unsigned i = 0; i < 6; ++i) if (packet.reserved[i]) return -RUM_EINVAL;
    struct rum_object_inheritance grants[RUM_OBJECT_HANDLE_LIMIT];
    uint32_t object_bytes = packet.object_count * sizeof grants[0];
    if (object_bytes && (!accessible(space, packet.objects, object_bytes, false) ||
        !paging_copy_from_user(space, grants, packet.objects, object_bytes))) return -RUM_EFAULT;
    struct rum_handle_mapping mappings[RUM_SPAWN_MAPPING_LIMIT];
    uint32_t bytes = request.mapping_count * sizeof mappings[0];
    if (bytes && (!accessible(space, request.mappings, bytes, false) ||
        !paging_copy_from_user(space, mappings, request.mappings, bytes))) return -RUM_EFAULT;
    char path[RUM_ABI_PATH_CAPACITY];
    rum_result_t error = copy_path(space, request.path, path);
    if (error) return error;
    if (!accessible(space, request.arguments, sizeof(struct rum_arguments), false)) return -RUM_EFAULT;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    struct rum_arguments *arguments = kmalloc(sizeof *arguments);
    if (!arguments) return -RUM_ENOMEM;
    if (!paging_copy_from_user(space, arguments, request.arguments, sizeof *arguments)) error = -RUM_EFAULT;
    else if (!elf_arguments_valid(arguments)) error = -RUM_EINVAL;
    task_id child = 0;
    if (!error) error = explicit_objects ?
        process_spawn_with_objects(path, arguments, mappings, request.mapping_count, request.flags, grants, packet.object_count, &child) :
        process_spawn_with_handles(path, arguments, mappings, request.mapping_count, request.flags, &child);
    (void)kfree(arguments);
    if (error) return error;
    struct task_information info;
    if (!task_query(child, &info)) cpu_halt();
    return (rum_result_t)info.process_id;
}

static rum_result_t wait_child(struct paging_space *space, uint32_t address)
{
    struct rum_wait_request request;
    struct rum_process_result output;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_LIFECYCLE_ABI_VERSION || request.size != sizeof request ||
        request.process_id > RUM_ABI_PID_MAX || (request.flags & ~RUM_WAIT_NOHANG) ||
        request.reserved[0] || request.reserved[1] || request.reserved[2]) return -RUM_EINVAL;
    /* Validate the whole output, including its input header, before sleeping
       or inspecting a child. It is revalidated after waking and copied before
       collection, so a failed copy cannot consume an exit record. */
    if (!accessible(space, request.result, sizeof output, true) ||
        !paging_copy_from_user(space, &output, request.result, sizeof output)) return -RUM_EFAULT;
    if (output.version != RUM_PROCESS_ABI_VERSION || output.size != sizeof output || output.reserved)
        return -RUM_EINVAL;
    struct task_information child;
    rum_result_t result = task_wait_child(request.process_id, request.flags & RUM_WAIT_NOHANG, &child);
    if (result != 1) return result;
    output = (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION, .size = sizeof output,
        .process_id = child.process_id, .termination = child.termination, .status = child.exit_status,
        .fault_vector = child.termination == TASK_TERMINATION_FAULT ? child.fault.vector : 0 };
    if (!accessible(space, request.result, sizeof output, true) ||
        !paging_copy_to_user(space, request.result, &output, sizeof output)) return -RUM_EFAULT;
    if (!task_collect_child(child.id)) cpu_halt();
    return 1;
}

static rum_result_t terminate_child(struct paging_space *space, uint32_t address)
{
    struct rum_terminate_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_LIFECYCLE_ABI_VERSION || request.size != sizeof request ||
        (request.flags & ~RUM_TERMINATE_SESSION) || request.reserved[0] || request.reserved[1]) return -RUM_EINVAL;
    return task_request_termination(request.process_id, request.flags & RUM_TERMINATE_SESSION);
}

static rum_result_t sleep_process(struct paging_space *space, uint32_t address)
{
    struct rum_sleep_request request;
    if (!accessible(space, address, sizeof request, false) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_SLEEP_ABI_VERSION || request.size != sizeof request ||
        request.flags || request.reserved[0] || request.reserved[1]) return -RUM_EINVAL;
    return task_sleep(request.milliseconds);
}

static rum_result_t process_list(struct paging_space *space, uint32_t address)
{
    struct rum_process_list_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_PROCESS_LIST_VERSION || request.size != sizeof request ||
        request.capacity > RUM_PROCESS_LIST_LIMIT || request.count || request.total ||
        request.reserved[0] || request.reserved[1] || (!request.capacity && request.entries)) return -RUM_EINVAL;
    uint32_t bytes = request.capacity * sizeof(struct rum_process_entry);
    if (!accessible(space, request.entries, bytes, true)) return -RUM_EFAULT;
    if (bytes && (uint64_t)request.entries < (uint64_t)address + sizeof request &&
        (uint64_t)address < (uint64_t)request.entries + bytes) return -RUM_EINVAL;
    struct rum_process_entry entries[RUM_PROCESS_LIST_LIMIT];
    request.count = task_process_list(entries, request.capacity, &request.total);
    if ((request.count && !paging_copy_to_user(space, request.entries, entries, request.count * sizeof entries[0])) ||
        !paging_copy_to_user(space, address, &request, sizeof request)) return -RUM_EFAULT;
    return (rum_result_t)request.count;
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

static rum_result_t job_control(struct paging_space *space, uint32_t address)
{
    struct rum_job_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    unsigned op = request.operation;
    if (request.version != RUM_JOB_ABI_VERSION || request.size != sizeof request ||
        op < RUM_JOB_CREATE || op > RUM_JOB_ACK || request.count || request.total ||
        request.reserved[0] || request.reserved[1] || request.job_id > RUM_ABI_PID_MAX) return -RUM_EINVAL;
    if (op == RUM_JOB_CREATE) {
        if (request.job_id || request.entries || request.capacity || (request.flags & ~RUM_JOB_FOREGROUND) || !request.name)
            return -RUM_EINVAL;
    } else if (request.name || (op != RUM_JOB_LIST && !request.job_id)) return -RUM_EINVAL;
    if (op == RUM_JOB_LIST) {
        if (request.job_id || request.flags || request.capacity > RUM_JOB_LIMIT || (!request.capacity && request.entries))
            return -RUM_EINVAL;
    } else if (op == RUM_JOB_WAIT) {
        if (request.capacity != 1 || (request.flags & ~RUM_JOB_WAIT_NOHANG)) return -RUM_EINVAL;
    } else if (op != RUM_JOB_CREATE && (request.flags || request.entries || request.capacity)) return -RUM_EINVAL;
    uint32_t bytes = request.capacity * sizeof(struct rum_job_entry);
    if (bytes && (!accessible(space, request.entries, bytes, true) ||
        ((uint64_t)request.entries < (uint64_t)address + sizeof request &&
         (uint64_t)address < (uint64_t)request.entries + bytes))) return -RUM_EFAULT;
    char name[RUM_JOB_NAME_CAPACITY] = {0};
    if (op == RUM_JOB_CREATE) {
        unsigned i = 0;
        for (; i < sizeof name; ++i) {
            if (i > UINT32_MAX - request.name || !accessible(space, request.name + i, 1, false) ||
                !paging_copy_from_user(space, name + i, request.name + i, 1)) return -RUM_EFAULT;
            if (!name[i]) break;
            if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126) return -RUM_EINVAL;
        }
        if (!i || i == sizeof name) return -RUM_EINVAL;
    }
    struct rum_job_entry entries[RUM_JOB_LIMIT];
    rum_result_t result = task_job_operation(&request, name, entries);
    if (result < 0) return result;
    if ((request.count && !paging_copy_to_user(space, request.entries, entries, request.count * sizeof entries[0])) ||
        !paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    return result;
}

static rum_result_t session_control(struct paging_space *space, uint32_t action, uint32_t address, uint32_t reserved)
{
    if (reserved || action < RUM_SESSION_CHDIR || action > RUM_SESSION_REBOOT ||
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

static rum_result_t terminal_events(struct rum_terminal_event *events, uint32_t capacity, uint32_t milliseconds)
{
    uint32_t ticks;
    if (!timer_duration_ticks(milliseconds, &ticks)) return -RUM_ERANGE;
    uint32_t deadline = timer_ticks() + ticks;
    for (;;) {
        if (task_current_cancelled()) return -RUM_EINTR;
        if (!terminal_session_owned(task_current_id())) return -RUM_EACCES;
        if (terminal_session_redraw() || (task_current_is_shell() && task_shell_has_completion())) return -RUM_EINTR;
        struct task_event *event = task_current_is_shell() ? task_shell_input_event() : keyboard_input_event();
        uint32_t observed = task_event_sequence(event), count = 0;
        uint32_t value;
        while (count < capacity && keyboard_read_event(&value)) {
            events[count++] = (struct rum_terminal_event){ .type = value < 128 ? RUM_TERM_TEXT_EVENT : RUM_TERM_KEY_EVENT,
                .value = value, .tick = timer_ticks() };
        }
        if (count) return (rum_result_t)count;
        if (!milliseconds) return 0;
        uint32_t now = timer_ticks();
        if (timer_deadline_reached(now, deadline)) return 0;
        if (task_wait_for(event, observed, deadline - now) == TASK_WAIT_INVALID) return -RUM_EBUSY;
    }
}
static rum_result_t terminal_control(struct paging_space *space, uint32_t address)
{
    struct rum_terminal_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    unsigned op = request.operation;
    if (request.version != RUM_TERMINAL_ABI_VERSION || request.size != sizeof request ||
        op < RUM_TERM_QUERY || op > RUM_TERM_STYLED_LINE || request.now_ticks || request.reserved[0] || request.reserved[1])
        return -RUM_EINVAL;
    uint32_t bytes = 0; bool output = false;
    if (op == RUM_TERM_QUERY) {
        if (request.flags || request.x || request.y || request.count || request.timeout_ms) return -RUM_EINVAL;
        bytes = sizeof(struct rum_terminal_capabilities); output = true;
    } else if (op == RUM_TERM_CELLS) {
        if (request.flags || request.timeout_ms || request.x >= TERMINAL_WIDTH || request.y >= TERMINAL_HEIGHT ||
            request.count > RUM_TERM_MAX_CELLS || request.count > TERMINAL_WIDTH - request.x) return -RUM_EINVAL;
        bytes = request.count * sizeof(struct rum_terminal_cell);
    } else if (op == RUM_TERM_READ) {
        if (request.flags || request.x || request.y || !request.count || request.count > RUM_TERM_MAX_EVENTS)
            return -RUM_EINVAL;
        if (request.timeout_ms > RUM_TERM_MAX_WAIT_MS) return -RUM_ERANGE;
        bytes = request.count * sizeof(struct rum_terminal_event); output = true;
    } else if (op == RUM_TERM_LINE || op == RUM_TERM_STYLED_LINE) {
        if (request.flags || request.y || request.timeout_ms || request.count > RUM_TERM_MAX_LINE || request.x > request.count)
            return -RUM_EINVAL;
        bytes = request.count * (op == RUM_TERM_STYLED_LINE ? sizeof(struct rum_terminal_cell) : 1u);
    } else {
        if (request.data || request.count || request.timeout_ms) return -RUM_EINVAL;
        if (op == RUM_TERM_CURSOR) {
            if (request.flags > 1 || request.x >= TERMINAL_WIDTH || request.y >= TERMINAL_HEIGHT) return -RUM_EINVAL;
        } else if (op == RUM_TERM_SCROLL) {
            if (request.flags || request.y || request.x > RUM_SCROLL_DOWN) return -RUM_EINVAL;
        } else if (op == RUM_TERM_COLOR) {
            if (request.flags || request.x > 15 || request.y > 15) return -RUM_EINVAL;
        } else if (request.x || request.y || (op == RUM_TERM_BEGIN ? request.flags > RUM_TERM_INLINE : request.flags)) return -RUM_EINVAL;
    }
    if (bytes && (!accessible(space, request.data, bytes, output) ||
        (output && (uint64_t)request.data < (uint64_t)address + sizeof request &&
         (uint64_t)address < (uint64_t)request.data + bytes))) return -RUM_EFAULT;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    uint32_t id = task_current_id();
    rum_result_t result = 0;
    if (op == RUM_TERM_QUERY) {
        struct terminal_state state = terminal_state();
        struct rum_terminal_capabilities caps = { .version = RUM_TERMINAL_ABI_VERSION, .size = sizeof caps,
            .width = TERMINAL_WIDTH, .height = TERMINAL_HEIGHT, .max_cells = RUM_TERM_MAX_CELLS,
            .max_events = RUM_TERM_MAX_EVENTS, .tick_hz = TIMER_HZ, .ticks = timer_ticks(), .features = RUM_TERM_FEATURES,
            .mode = terminal_session_owner() ? RUM_TERM_ACTIVE | (terminal_session_owned(id) ? RUM_TERM_OWNED : 0) |
                (terminal_session_inline() ? RUM_TERM_INLINE_MODE : 0) | (terminal_scrolled() ? RUM_TERM_SCROLLED : 0) : 0,
            .cursor_x = state.x, .cursor_y = state.y, .foreground = state.foreground,
            .background = state.background, .cursor_visible = state.visible && !terminal_scrolled() };
        if (!paging_copy_to_user(space, request.data, &caps, sizeof caps)) cpu_halt();
    } else if (op == RUM_TERM_BEGIN) {
        struct process_handles *handles = task_current_handles();
        struct process_handle *in = process_handle_get(handles, RUM_STDIN), *out = process_handle_get(handles, RUM_STDOUT);
        if (!task_current_owns_console() || !in || !out || in->kind != HANDLE_INPUT || out->kind != HANDLE_OUTPUT)
            return -RUM_EACCES;
        result = request.flags & RUM_TERM_INLINE ? terminal_session_begin_inline(id) : terminal_session_begin(id);
    } else {
        if (!terminal_session_owned(id)) return -RUM_EACCES;
        if (op == RUM_TERM_END) result = terminal_session_end(id);
        else if (op == RUM_TERM_SCROLL) {
            if (!terminal_session_inline()) return -RUM_EACCES;
            terminal_scroll(request.x == RUM_SCROLL_UP ? -1 : request.x == RUM_SCROLL_DOWN ? 1 : 0);
        }
        else if (op == RUM_TERM_LINE) {
            if (!terminal_session_inline()) return -RUM_EACCES;
            char text[RUM_TERM_MAX_LINE];
            if (bytes && !paging_copy_from_user(space, text, request.data, bytes)) return -RUM_EFAULT;
            for (uint32_t i = 0; i < bytes; ++i) if (text[i] < 32 || text[i] > 126) return -RUM_EINVAL;
            terminal_session_line(text, bytes, request.x);
        } else if (op == RUM_TERM_STYLED_LINE) {
            if (!terminal_session_inline()) return -RUM_EACCES;
            struct rum_terminal_cell cells[RUM_TERM_MAX_LINE];
            if (bytes && !paging_copy_from_user(space, cells, request.data, bytes)) return -RUM_EFAULT;
            for (uint32_t i = 0; i < request.count; ++i)
                if (cells[i].character < 32 || cells[i].character > 126 || cells[i].foreground > 15 ||
                    cells[i].background > 15 || cells[i].reserved) return -RUM_EINVAL;
            terminal_session_styled_line(cells, request.count, request.x);
        }
        else if (op == RUM_TERM_CURSOR) {
            (void)terminal_cursor_at(request.x, request.y); terminal_cursor_visible(request.flags != 0);
        } else if (op == RUM_TERM_COLOR) {
            terminal_set_color((enum vga_color)request.x, (enum vga_color)request.y);
        } else if (op == RUM_TERM_CELLS) {
            struct rum_terminal_cell cells[RUM_TERM_MAX_CELLS];
            if (bytes && !paging_copy_from_user(space, cells, request.data, bytes)) return -RUM_EFAULT;
            /* Validate the complete row before the first framebuffer update. */
            for (uint32_t i = 0; i < request.count; ++i)
                if (cells[i].character < 32 || cells[i].character > 126 || cells[i].foreground > 15 ||
                    cells[i].background > 15 || cells[i].reserved) return -RUM_EINVAL;
            for (uint32_t i = 0; i < request.count; ++i)
                (void)terminal_put_at(request.x + i, request.y, (char)cells[i].character,
                    (enum vga_color)cells[i].foreground, (enum vga_color)cells[i].background);
        } else if (op == RUM_TERM_READ) {
            struct rum_terminal_event events[RUM_TERM_MAX_EVENTS];
            result = terminal_events(events, request.count, request.timeout_ms);
            if (result > 0 && !paging_copy_to_user(space, request.data, events, (uint32_t)result * sizeof events[0])) cpu_halt();
        }
    }
    if (result < 0) return result;
    request.now_ticks = timer_ticks();
    if (!paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    return result;
}

static rum_result_t console_action(uint32_t action, uint32_t reserved1, uint32_t reserved2)
{
    if (reserved1 || reserved2 || action > RUM_CONSOLE_SNAKE) return -RUM_EINVAL;
    if (!task_current_owns_console()) return -RUM_EACCES;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (action == RUM_CONSOLE_CLEAR) {
        if (!task_switch_hold()) return -RUM_EBUSY;
        terminal_clear(); serial_writestring("\x1b[2J\x1b[H");
        if (!task_switch_release()) cpu_halt();
        return 0;
    }
    return -RUM_ENOSYS; /* Snake is an ELF; the kernel game is recovery-only. */
}

static rum_result_t clock_snapshot(struct paging_space *space, uint32_t address)
{
    struct rum_clock_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_CLOCK_ABI_VERSION || request.size != sizeof request ||
        request.ticks_lo || request.ticks_hi || request.tick_hz ||
        request.reserved[0] || request.reserved[1] || request.reserved[2]) return -RUM_EINVAL;
    uint64_t ticks = timer_ticks_extended();
    request.ticks_lo = (uint32_t)ticks; request.ticks_hi = (uint32_t)(ticks >> 32);
    request.tick_hz = TIMER_HZ;
    if (!paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    return 0;
}

static rum_result_t object_control(struct paging_space *space, uint32_t address)
{
    struct rum_object_request request;
    /* Every output byte is writable before a table or backend can change.
       The dispatcher's mapping lease survives a yielding final destructor. */
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    rum_result_t result = object_request_validate(&request);
    if (!result) result = request.operation == RUM_OBJECT_FROM_DESCRIPTOR ?
        process_handle_export(task_current_handles(), request.handle, task_current_objects(), request.type,
                              request.rights, request.flags, &request.result_handle) :
        object_handle_request(task_current_objects(), &request);
    if (!result && !paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    return result;
}

static rum_result_t object_io(struct paging_space *space, uint32_t address)
{
    struct rum_object_io_request request;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_OBJECT_IO_VERSION || request.size != sizeof request ||
        request.operation < RUM_OBJECT_IO_READ || request.operation > RUM_OBJECT_IO_ISATTY ||
        (request.type != RUM_OBJECT_ANY && !object_rights_mask(request.type)) || request.reserved[0] || request.reserved[1])
        return -RUM_EINVAL;
    bool transfer_call = request.operation == RUM_OBJECT_IO_READ || request.operation == RUM_OBJECT_IO_WRITE;
    bool seek = request.operation == RUM_OBJECT_IO_SEEK;
    if ((!transfer_call && request.bytes) || (seek ? request.whence > RUM_SEEK_END : request.whence || request.offset_lo || request.offset_hi) ||
        ((!transfer_call && request.operation != RUM_OBJECT_IO_READDIR) && request.data)) return -RUM_EINVAL;
    /* Structured output must not overwrite its own control packet. */
    if (request.operation == RUM_OBJECT_IO_READDIR &&
        (uint64_t)request.data < (uint64_t)address + sizeof request &&
        (uint64_t)address < (uint64_t)request.data + sizeof(struct rum_directory_entry)) return -RUM_EINVAL;
    uint32_t right = request.operation == RUM_OBJECT_IO_READ ? RUM_RIGHT_READ :
        request.operation == RUM_OBJECT_IO_WRITE ? RUM_RIGHT_WRITE : seek ? RUM_RIGHT_SEEK :
        request.operation == RUM_OBJECT_IO_READDIR ? RUM_RIGHT_ENUMERATE :
        request.operation == RUM_OBJECT_IO_FLUSH ? RUM_RIGHT_FLUSH : RUM_RIGHT_QUERY;
    struct kernel_object *object;
    rum_result_t result = object_handle_acquire(task_current_objects(), request.handle, request.type, right, &object);
    if (result) return result;
    result = retain_io(object);
    if (result) return result;
    struct process_handle *handle = process_handle_from_object(object);
    if (!handle) return -RUM_ENOSYS;
    if (transfer_call) return transfer_handle(space, handle, request.data, request.bytes, request.operation == RUM_OBJECT_IO_WRITE);
    if (seek) {
        uint64_t position;
        result = process_handle_seek(handle, (uint64_t)request.offset_hi << 32 | request.offset_lo, request.whence, &position);
        if (!result) {
            request.offset_lo = (uint32_t)position; request.offset_hi = (uint32_t)(position >> 32);
            if (!paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
        }
        return result;
    }
    if (request.operation == RUM_OBJECT_IO_READDIR) return list_directory_handle(space, handle, request.data);
    if (request.operation == RUM_OBJECT_IO_FLUSH) return process_handle_flush(handle);
    return handle->kind == HANDLE_INPUT || handle->kind == HANDLE_OUTPUT;
}

static rum_result_t process_object_control(struct paging_space *space, uint32_t address)
{
    struct rum_process_object_request request;
    struct rum_process_object_information output;
    if (!accessible(space, address, sizeof request, true) ||
        !paging_copy_from_user(space, &request, address, sizeof request)) return -RUM_EFAULT;
    if (request.version != RUM_PROCESS_OBJECT_VERSION || request.size != sizeof request ||
        request.operation < RUM_PROCESS_OBJECT_OPEN || request.operation > RUM_PROCESS_OBJECT_TERMINATE ||
        request.result_handle || request.reserved[0] || request.reserved[1] || request.reserved[2]) return -RUM_EINVAL;
    bool open = request.operation == RUM_PROCESS_OBJECT_OPEN;
    bool query = request.operation == RUM_PROCESS_OBJECT_QUERY;
    bool wait = request.operation == RUM_PROCESS_OBJECT_WAIT;
    if (open ? !request.process_id || request.process_id > RUM_ABI_PID_MAX || request.handle || request.data ||
               (request.rights & ~RUM_RIGHT_PROCESS) || (request.flags & ~(RUM_PROCESS_OPEN_SESSION | RUM_PROCESS_OPEN_INHERIT)) :
               request.process_id || request.rights || (request.flags & ~(wait ? RUM_PROCESS_OBJECT_NOHANG : 0u)) ||
               (!query && !wait && request.data)) return -RUM_EINVAL;
    if (query || wait) {
        if ((uint64_t)request.data < (uint64_t)address + sizeof request &&
            (uint64_t)address < (uint64_t)request.data + sizeof output) return -RUM_EINVAL;
        if (!accessible(space, request.data, sizeof output, true) ||
            !paging_copy_from_user(space, &output, request.data, sizeof output)) return -RUM_EFAULT;
        if (output.version != RUM_PROCESS_OBJECT_VERSION || output.size != sizeof output ||
            output.reserved[0] || output.reserved[1] || output.reserved[2]) return -RUM_EINVAL;
    }
    rum_result_t result = task_process_object_operation(&request, &output);
    if (!result && open && !paging_copy_to_user(space, address, &request, sizeof request)) cpu_halt();
    if (((query && !result) || (wait && result == 1)) &&
        !paging_copy_to_user(space, request.data, &output, sizeof output)) cpu_halt();
    return result;
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
    terminal_session_reconcile();
    task_cancel_current_if_requested();
    struct paging_space *space = task_current_process_space();
    struct process_handles *handles = task_current_handles();
    rum_result_t result;
    bool memory_call = frame->eax == RUM_SYS_VM_ALLOCATE || frame->eax == RUM_SYS_VM_RELEASE ||
                       frame->eax == RUM_SYS_VM_PROTECT;
    bool needs_buffers = !memory_call && frame->eax != RUM_SYS_EXIT;
    bool retained = needs_buffers && task_user_buffers_begin();
    if (needs_buffers && !retained) result = -RUM_EBUSY;
    else switch (frame->eax) {
    case RUM_SYS_PROCESS_OBJECT:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : process_object_control(space, frame->ebx); break;
    case RUM_SYS_OBJECT_IO:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : object_io(space, frame->ebx); break;
    case RUM_SYS_OBJECT:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : object_control(space, frame->ebx); break;
    case RUM_SYS_CLOCK:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : clock_snapshot(space, frame->ebx); break;
    case RUM_SYS_VM_ALLOCATE: case RUM_SYS_VM_RELEASE: case RUM_SYS_VM_PROTECT:
        result = frame->ecx || frame->edx ? -RUM_EINVAL :
            memory_region(space, frame->ebx, frame->eax,
                          exception_frame_esp(frame)); break;
    case RUM_SYS_EXIT: task_exit_with_status((rum_result_t)frame->ebx);
    case RUM_SYS_GETPID: result = (rum_result_t)task_current_process_id(); break;
    case RUM_SYS_READ: case RUM_SYS_WRITE:
        result = transfer(space, handles, frame->ebx, frame->ecx, frame->edx, frame->eax == RUM_SYS_WRITE); break;
    case RUM_SYS_OPEN: result = open_file(space, handles, frame->ebx); break;
    case RUM_SYS_STAT:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : file_information(space, handles, frame->ebx); break;
    case RUM_SYS_RENAME:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : rename_file(space, frame->ebx); break;
    case RUM_SYS_CLOSE: result = process_handle_close(handles, frame->ebx); break;
    case RUM_SYS_TERMINAL:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : terminal_control(space, frame->ebx); break;
    case RUM_SYS_JOB:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : job_control(space, frame->ebx); break;
    case RUM_SYS_ISATTY: {
        struct process_handle *handle;
        result = frame->ecx || frame->edx ? -RUM_EINVAL : retain_descriptor(handles, frame->ebx, &handle);
        if (!result) result = handle->kind == HANDLE_INPUT || handle->kind == HANDLE_OUTPUT;
        break;
    }
    case RUM_SYS_PIPE:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : create_pipe(space, handles, frame->ebx); break;
    case RUM_SYS_DUPLICATE:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : duplicate_handle(space, handles, frame->ebx); break;
    case RUM_SYS_SPAWN_MAPPED:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : spawn_mapped_child(space, frame->ebx); break;
    case RUM_SYS_SEEK: result = seek_file(space, handles, frame->ebx, frame->ecx); break;
    case RUM_SYS_READDIR: result = list_directory(space, handles, frame->ebx, frame->ecx); break;
    case RUM_SYS_CHDIR: case RUM_SYS_MKDIR: case RUM_SYS_REMOVE:
        result = path_operation(space, frame->ebx, frame->eax); break;
    case RUM_SYS_GETCWD: result = working_directory(space, frame->ebx, frame->ecx); break;
    case RUM_SYS_FLUSH: {
        struct process_handle *handle;
        result = retain_descriptor(handles, frame->ebx, &handle);
        if (!result) result = process_handle_flush(handle);
        break;
    }
    case RUM_SYS_RUN: result = run_child(space, frame->ebx); break;
    case RUM_SYS_YIELD:
        result = frame->ebx || frame->ecx || frame->edx ? -RUM_EINVAL : task_yield() ? 0 : -RUM_EBUSY;
        break;
    case RUM_SYS_SLEEP:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : sleep_process(space, frame->ebx);
        break;
    case RUM_SYS_PROCESS_LIST:
        result = frame->ecx || frame->edx ? -RUM_EINVAL : process_list(space, frame->ebx);
        break;
    case RUM_SYS_SPAWN: case RUM_SYS_WAIT: case RUM_SYS_TERMINATE:
        result = frame->ecx || frame->edx ? -RUM_EINVAL :
            frame->eax == RUM_SYS_SPAWN ? spawn_child(space, frame->ebx) :
            frame->eax == RUM_SYS_WAIT ? wait_child(space, frame->ebx) : terminate_child(space, frame->ebx);
        break;
    case RUM_SYS_REPLACE: result = replace_file(space, frame->ebx); break;
    case RUM_SYS_CONSOLE: result = console_action(frame->ebx, frame->ecx, frame->edx); break;
    case RUM_SYS_COMMAND_TEXT: result = command_text(space, frame->ebx, frame->ecx, frame->edx); break;
    case RUM_SYS_SESSION: result = session_control(space, frame->ebx, frame->ecx, frame->edx); break;
    default: result = -RUM_ENOSYS; break;
    }
    if (!task_io_end()) cpu_halt();
    if (retained && !task_user_buffers_end()) cpu_halt();
    frame->eax = (uint32_t)result;
    terminal_session_reconcile();
    task_exit_shell_if_requested();
}
