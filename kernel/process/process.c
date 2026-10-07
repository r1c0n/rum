#include <stddef.h>
#include <rum/elf.h>
#include <rum/abi/error.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/process.h>
#include <rum/process_limits.h>
#include <rum/ramfs.h>

rum_result_t process_start_foreground(const char *program, const struct rum_arguments *arguments, task_id *child)
{
    if (!child || !program || !elf_arguments_valid(arguments)) return -RUM_EINVAL;
    *child = 0;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    char path[FS_PATH_CAPACITY]; size_t length = 0;
    while (length < sizeof path && program[length]) ++length;
    if (!length || length == sizeof path) return -RUM_ENAMETOOLONG;
    memcpy(path, program, length + 1);
    fs_reference file = 0;
    enum fs_error error = fs_open(task_current_filesystem(), path, FS_READ, &file);
    bool has_suffix = length >= 4 && path[length - 4] == '.' &&
        (path[length - 3] == 'e' || path[length - 3] == 'E') &&
        (path[length - 2] == 'l' || path[length - 2] == 'L') &&
        (path[length - 1] == 'f' || path[length - 1] == 'F');
    if (error == FS_NOT_FOUND && !has_suffix) {
        if (length + 5 > sizeof path) return -RUM_ENAMETOOLONG;
        memcpy(path + length, ".elf", 5);
        error = fs_open(task_current_filesystem(), path, FS_READ, &file);
    }
    if (error != FS_OK) return process_fs_error(error);
    struct fs_information info;
    error = fs_stat(file, &info);
    rum_result_t result = process_fs_error(error);
    if (!result && (info.kind != FS_FILE || !info.size || info.size > RAMFS_FILE_LIMIT)) result = -RUM_ENOEXEC;
    void *image = !result ? kmalloc((size_t)info.size) : NULL;
    if (!result && !image) result = -RUM_ENOMEM;
    size_t read = 0;
    while (!result && read < info.size) {
        struct fs_io_result io = fs_read(file, read, (char *)image + read, (size_t)info.size - read);
        read += io.transferred;
        if (io.error != FS_OK) result = process_fs_error(io.error);
        else if (!io.transferred) result = -RUM_EIO;
    }
    enum fs_error closed = fs_close(file);
    if (!result) result = process_fs_error(closed);
    struct task_process process = {0};
    if (!result && !elf_load_process(image, (size_t)info.size, arguments, &process)) result = -RUM_ENOEXEC;
    (void)kfree(image);
    if (result) return result;
    if (task_current_cancelled()) {
        (void)paging_space_destroy(process.space);
        return -RUM_EINTR;
    }
    *child = task_create_foreground_process(&process);
    if (!*child) { (void)paging_space_destroy(process.space); return -RUM_ENOMEM; }
    return 0;
}

rum_result_t process_run_foreground(const char *path, const struct rum_arguments *arguments, struct process_result *result)
{
    if (!result) return -RUM_EINVAL;
    *result = (struct process_result){0};
    task_id child;
    rum_result_t error = process_start_foreground(path, arguments, &child);
    if (error) return error;
    struct task_information info;
    if (!task_wait_process(child, &info) || !task_foreground_end(child)) return -RUM_EIO;
    *result = (struct process_result){ info.process_id, info.termination, info.exit_status, info.fault };
    if (!task_reap_process(child)) return -RUM_EIO;
    return 0;
}

static bool add_argument(struct rum_arguments *packet, const char *text, size_t bytes)
{
    if (!bytes || packet->argc >= RUM_PROCESS_ARGUMENT_LIMIT ||
        bytes >= RUM_PROCESS_ARGUMENT_BYTES - packet->string_bytes) return false;
    uint32_t offset = packet->string_bytes;
    packet->offsets[packet->argc++] = offset;
    memcpy(packet->strings + offset, text, bytes);
    packet->strings[offset + bytes] = '\0';
    packet->string_bytes += (uint32_t)bytes + 1;
    return true;
}

static bool build_arguments(const char *program, const char *text,
                            struct rum_arguments *packet)
{
    if (!program || !*program || !text || !packet) return false;
    *packet = (struct rum_arguments){0};
    size_t bytes = 0;
    while (program[bytes]) ++bytes;
    if (!add_argument(packet, program, bytes)) return false;

    while (*text) {
        while (*text == ' ' || *text == '\t') ++text;
        if (!*text) break;
        const char *word = text;
        while (*text && *text != ' ' && *text != '\t') ++text;
        if (!add_argument(packet, word, (size_t)(text - word))) return false;
    }
    return true;
}

static bool executable_name(const char *program, char name[RAMFS_NAME_CAPACITY])
{
    size_t length = 0;
    while (program[length] && length < RAMFS_NAME_CAPACITY) ++length;
    if (!length || length >= RAMFS_NAME_CAPACITY) return false;
    const unsigned char *data;
    size_t size;
    if (ramfs_read(program, &data, &size)) {
        memcpy(name, program, length + 1);
        return true;
    }
    static const char suffix[] = ".elf";
    if (length >= sizeof suffix - 1 &&
        !memcmp(program + length - (sizeof suffix - 1), suffix, sizeof suffix - 1)) return false;
    if (length + sizeof suffix > RAMFS_NAME_CAPACITY) return false;
    memcpy(name, program, length);
    memcpy(name + length, suffix, sizeof suffix);
    return ramfs_read(name, &data, &size);
}

enum process_launch_error process_launch_foreground(const char *program,
                                                     const char *arguments,
                                                     struct process_result *result)
{
    if (!result) return PROCESS_LAUNCH_INTERNAL;
    *result = (struct process_result){0};
    struct rum_arguments packet;
    if (!build_arguments(program, arguments, &packet))
        return PROCESS_LAUNCH_INVALID_ARGUMENTS;

    char name[RAMFS_NAME_CAPACITY];
    struct task_process process;
    if (!executable_name(program, name) || !elf_load_ramfs(name, &packet, &process))
        return PROCESS_LAUNCH_EXECUTABLE;

    task_id child = task_create_foreground_process(&process);
    if (!child) {
        (void)paging_space_destroy(process.space);
        return PROCESS_LAUNCH_RESOURCES;
    }

    struct task_information information;
    if (!task_wait_process(child, &information) || !task_foreground_end(child))
        return PROCESS_LAUNCH_INTERNAL;
    *result = (struct process_result){
        .process_id = information.process_id,
        .termination = information.termination,
        .exit_status = information.exit_status,
        .fault = information.fault,
    };
    if (!task_reap_process(child)) return PROCESS_LAUNCH_INTERNAL;
    return PROCESS_LAUNCH_OK;
}
