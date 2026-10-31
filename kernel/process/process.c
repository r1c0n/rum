#include <stddef.h>
#include <rum/elf.h>
#include <rum/cpu.h>
#include <rum/abi/error.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/process.h>
#include <rum/process_limits.h>

static rum_result_t start_process(const char *program, const struct rum_arguments *arguments,
                                  bool foreground, bool shell, task_id *child)
{
    if (!child || !program || !elf_arguments_valid(arguments)) return -RUM_EINVAL;
    *child = 0;
    if (shell && task_current_is_process()) return -RUM_EACCES;
    rum_result_t status = task_process_creation_status(foreground);
    if (status) return status;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    char path[FS_PATH_CAPACITY]; size_t length = 0;
    while (length < sizeof path && program[length]) ++length;
    if (!length || length == sizeof path) return -RUM_ENAMETOOLONG;
    memcpy(path, program, length + 1);
    fs_reference file = 0;
    enum fs_error error = fs_open(task_current_filesystem(), path, FS_READ, &file);
    bool has_extension = false;
    for (size_t i = 0; i < length; ++i) {
        if (path[i] == '/') has_extension = false;
        else if (path[i] == '.') has_extension = true;
    }
    if (error == FS_NOT_FOUND && !has_extension) {
        if (length + 5 > sizeof path) return -RUM_ENAMETOOLONG;
        memcpy(path + length, ".elf", 5);
        error = fs_open(task_current_filesystem(), path, FS_READ, &file);
    }
    if (error != FS_OK) return process_fs_error(error);
    struct fs_information info;
    error = fs_stat(file, &info);
    rum_result_t result = process_fs_error(error);
    if (!result && (info.kind != FS_FILE || !info.size || info.size > RUM_ELF_IMAGE_LIMIT)) result = -RUM_ENOEXEC;
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
    const char *name = path;
    for (unsigned i = 0; path[i]; ++i) if (path[i] == '/') name = path + i + 1;
    for (unsigned i = 0; i < sizeof process.name - 1 && name[i]; ++i)
        process.name[i] = (unsigned char)name[i] >= 32 && (unsigned char)name[i] <= 126 ? name[i] : '?';
    *child = shell ? task_create_shell_process(&process) :
        foreground ? task_create_foreground_process(&process) : task_create_process(&process);
    if (!*child) { (void)paging_space_destroy(process.space); return -RUM_ENOMEM; }
    return 0;
}

rum_result_t process_start_foreground(const char *path, const struct rum_arguments *arguments, task_id *child)
{
    return start_process(path, arguments, true, false, child);
}

rum_result_t process_start_shell(const char *path, const struct rum_arguments *arguments, task_id *child)
{
    return start_process(path, arguments, true, true, child);
}

rum_result_t process_spawn(const char *path, const struct rum_arguments *arguments, task_id *child)
{
    return start_process(path, arguments, false, false, child);
}

static rum_result_t run_foreground(const char *path, const struct rum_arguments *arguments,
                                   const char *command_text, struct process_result *result)
{
    if (!result) return -RUM_EINVAL;
    if (command_text && !task_current_is_shell()) return -RUM_EACCES;
    *result = (struct process_result){0};
    task_id child;
    rum_result_t error = process_start_foreground(path, arguments, &child);
    if (error) return error;
    if (command_text && !task_mark_shell_command(child, command_text)) cpu_halt();
    struct task_information info;
    if (!task_query(child, &info)) cpu_halt();
    error = task_wait_child(info.process_id, false, &info);
    if (error != 1) return error < 0 ? error : -RUM_EIO;
    *result = (struct process_result){ info.process_id, info.termination, info.exit_status, info.fault };
    if (!task_collect_child(child)) cpu_halt();
    return 0;
}

rum_result_t process_run_foreground(const char *path, const struct rum_arguments *arguments, struct process_result *result)
{
    return run_foreground(path, arguments, NULL, result);
}

rum_result_t process_run_command(const char *path, const struct rum_arguments *arguments,
                                const char *text, struct process_result *result)
{
    if (!text) return -RUM_EINVAL;
    return run_foreground(path, arguments, text, result);
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

enum process_launch_error process_launch_foreground(const char *program,
                                                     const char *arguments,
                                                     struct process_result *result)
{
    if (!result) return PROCESS_LAUNCH_INTERNAL;
    *result = (struct process_result){0};
    struct rum_arguments packet;
    if (!build_arguments(program, arguments, &packet))
        return PROCESS_LAUNCH_INVALID_ARGUMENTS;

    char path[FS_PATH_CAPACITY];
    size_t length = 0; bool slash = false;
    while (program[length]) { if (program[length] == '/') slash = true; ++length; }
    unsigned prefix = slash ? 0 : 5;
    if (length + prefix >= sizeof path) return PROCESS_LAUNCH_INVALID_ARGUMENTS;
    if (prefix) memcpy(path, "/rum/", prefix);
    memcpy(path + prefix, program, length + 1);
    rum_result_t error = process_run_foreground(path, &packet, result);
    return !error ? PROCESS_LAUNCH_OK : error == -RUM_ENOMEM ? PROCESS_LAUNCH_RESOURCES :
           error == -RUM_EIO ? PROCESS_LAUNCH_INTERNAL : PROCESS_LAUNCH_EXECUTABLE;
}
