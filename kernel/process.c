#include <stddef.h>
#include <rum/elf.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/process.h>
#include <rum/process_limits.h>
#include <rum/ramfs.h>

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
