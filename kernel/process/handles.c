#include <rum/abi/error.h>
#include <rum/handles.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/process_limits.h>
#include <rum/pipe.h>

#define OPEN_DESCRIPTIONS (RUM_PROCESS_LIMIT * RUM_ABI_HANDLE_LIMIT)
static struct process_handle descriptions[OPEN_DESCRIPTIONS];

_Static_assert(RUM_OPEN_READ == FS_READ && RUM_OPEN_WRITE == FS_WRITE, "access bits");
_Static_assert(RUM_OPEN_CREATE == FS_OPEN_CREATE && RUM_OPEN_TRUNCATE == FS_OPEN_TRUNCATE &&
               RUM_OPEN_APPEND == FS_OPEN_APPEND && RUM_OPEN_EXCLUSIVE == FS_OPEN_EXCLUSIVE, "open option bits");
_Static_assert(RUM_ABI_HANDLE_LIMIT == 32 && RUM_SPAWN_MAPPING_LIMIT <= 32,
               "mapping conflict bitmap covers every descriptor");

bool process_filesystem_context(void)
{
    if (irq_in_handler()) return false;
    uint32_t flags = cpu_interrupt_save();
    cpu_interrupt_restore(flags);
    return (flags & 0x200u) != 0;
}

rum_result_t process_fs_error(enum fs_error error)
{
    switch (error) {
    case FS_OK: case FS_END: return 0;
    case FS_INVALID: case FS_MOUNT_ESCAPE: return -RUM_EINVAL;
    case FS_UNAVAILABLE: return -RUM_ENODEV;
    case FS_NOT_FOUND: return -RUM_ENOENT;
    case FS_NOT_DIRECTORY: return -RUM_ENOTDIR;
    case FS_IS_DIRECTORY: return -RUM_EISDIR;
    case FS_EXISTS: return -RUM_EEXIST;
    case FS_NOT_EMPTY: return -RUM_ENOTEMPTY;
    case FS_PATH_TOO_LONG: case FS_NAME_TOO_LONG: case FS_TOO_DEEP: return -RUM_ENAMETOOLONG;
    case FS_RANGE: return -RUM_ERANGE;
    case FS_ACCESS: return -RUM_EACCES;
    case FS_READ_ONLY: return -RUM_EROFS;
    case FS_BUSY: return -RUM_EBUSY;
    case FS_NO_MEMORY: return -RUM_ENOMEM;
    case FS_NO_SPACE: return -RUM_ENOSPC;
    case FS_UNSUPPORTED: return -RUM_ENOSYS;
    case FS_CROSS_DEVICE: return -RUM_EXDEV;
    case FS_TIMEOUT: return -RUM_ETIMEDOUT;
    case FS_IO_ERROR: case FS_DEVICE_FAULT: return -RUM_EIO;
    }
    return -RUM_EIO;
}

static struct process_handle *new_description(enum process_handle_kind kind, unsigned access)
{
    uint32_t flags = cpu_interrupt_save();
    struct process_handle *result = NULL;
    for (unsigned i = 0; i < OPEN_DESCRIPTIONS; ++i) if (!descriptions[i].references) {
        result = &descriptions[i];
        *result = (struct process_handle){ .kind = kind, .access = access, .references = 1 };
        break;
    }
    cpu_interrupt_restore(flags);
    return result;
}

static rum_result_t retain(struct process_handle *object)
{
    uint32_t flags = cpu_interrupt_save();
    rum_result_t error = object->references == UINT32_MAX ? -RUM_EOVERFLOW : 0;
    if (!error) ++object->references;
    cpu_interrupt_restore(flags);
    return error;
}

static rum_result_t release(struct process_handle *object)
{
    if (object->references == 1 && object->reference) {
        enum fs_error error = fs_close(object->reference);
        if (error != FS_OK) return process_fs_error(error);
    }
    uint32_t flags = cpu_interrupt_save();
    if (object->references == 1 && object->pipe && !pipe_close(object->pipe, object->kind == HANDLE_PIPE_WRITE)) {
        cpu_interrupt_restore(flags); return -RUM_EIO;
    }
    if (!--object->references) *object = (struct process_handle){0};
    cpu_interrupt_restore(flags);
    return 0;
}

struct process_handle_statistics process_handles_stats(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct process_handle_statistics result = {0};
    for (unsigned i = 0; i < OPEN_DESCRIPTIONS; ++i) if (descriptions[i].references) {
        ++result.descriptions;
        result.descriptors += descriptions[i].references;
    }
    result.aliases = result.descriptors - result.descriptions;
    cpu_interrupt_restore(flags);
    return result;
}

rum_result_t process_handles_initialize(struct process_handles *table)
{
    if (!table) return -RUM_EINVAL;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    *table = (struct process_handles){0};
    for (unsigned i = 0; i < 3; ++i) {
        table->slots[i].object = new_description(i ? HANDLE_OUTPUT : HANDLE_INPUT, i ? FS_WRITE : FS_READ);
        if (!table->slots[i].object) { (void)process_handles_destroy(table); return -RUM_ENOMEM; }
    }
    return 0;
}

unsigned process_handles_count(const struct process_handles *table)
{
    unsigned count = 0;
    if (table) for (unsigned i = 0; i < RUM_ABI_HANDLE_LIMIT; ++i) count += table->slots[i].object != NULL;
    return count;
}

struct process_handle *process_handle_get(struct process_handles *table, rum_handle_t handle)
{
    return table && handle < RUM_ABI_HANDLE_LIMIT ? table->slots[handle].object : NULL;
}

rum_result_t process_handle_open(struct process_handles *table, const struct fs_context *context,
                                 const char *path, unsigned access, unsigned flags)
{
    if (!table || !path || !access || (access & ~(FS_READ | FS_WRITE)) ||
        (flags & ~(RUM_OPEN_DIRECTORY | RUM_OPEN_CREATE | RUM_OPEN_TRUNCATE | RUM_OPEN_APPEND | RUM_OPEN_EXCLUSIVE)) ||
        ((flags & RUM_OPEN_EXCLUSIVE) && !(flags & RUM_OPEN_CREATE)) ||
        ((flags & RUM_OPEN_DIRECTORY) && (access != FS_READ || flags != RUM_OPEN_DIRECTORY)) ||
        ((flags & ~RUM_OPEN_DIRECTORY) && !(access & FS_WRITE))) return -RUM_EINVAL;
    unsigned slot = 3;
    while (slot < RUM_ABI_HANDLE_LIMIT && table->slots[slot].object) ++slot;
    if (slot == RUM_ABI_HANDLE_LIMIT) return -RUM_EMFILE;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    struct process_handle *object = new_description(HANDLE_FILE, access);
    if (!object) return -RUM_ENOMEM;
    fs_reference reference = 0;
    enum fs_error error = fs_open_flags(context, path, access, flags & ~RUM_OPEN_DIRECTORY, &reference);
    if (error != FS_OK) { (void)release(object); return process_fs_error(error); }
    struct fs_information info;
    error = fs_stat(reference, &info);
    if (error == FS_OK && (flags & RUM_OPEN_DIRECTORY) && info.kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
    if (error != FS_OK) { (void)fs_close(reference); (void)release(object); return process_fs_error(error); }
    uint32_t saved = cpu_interrupt_save();
    object->reference = reference;
    object->append = (flags & RUM_OPEN_APPEND) != 0;
    object->kind = info.kind == FS_DIRECTORY ? HANDLE_DIRECTORY : HANDLE_FILE;
    table->slots[slot] = (struct process_descriptor){ .object = object };
    cpu_interrupt_restore(saved);
    return (rum_result_t)slot;
}

rum_result_t process_handle_close(struct process_handles *table, rum_handle_t id)
{
    struct process_handle *handle = process_handle_get(table, id);
    if (!handle) return -RUM_EBADF;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (handle->references == 1 && handle->reference) {
        enum fs_error error = fs_close(handle->reference);
        if (error != FS_OK) return process_fs_error(error);
    }
    uint32_t saved = cpu_interrupt_save();
    if (handle->references == 1 && handle->pipe && !pipe_close(handle->pipe, handle->kind == HANDLE_PIPE_WRITE)) {
        cpu_interrupt_restore(saved); return -RUM_EIO;
    }
    table->slots[id] = (struct process_descriptor){0};
    if (!--handle->references) *handle = (struct process_handle){0};
    cpu_interrupt_restore(saved);
    return 0;
}

rum_result_t process_handles_pipe(struct process_handles *table, rum_handle_t *reader, rum_handle_t *writer)
{
    if (!table || !reader || !writer || reader == writer) return -RUM_EINVAL;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    unsigned slots[2], count = 0;
    for (unsigned i = 3; i < RUM_ABI_HANDLE_LIMIT && count < 2; ++i)
        if (!table->slots[i].object) slots[count++] = i;
    if (count != 2) return -RUM_EMFILE;
    struct process_handle *read = new_description(HANDLE_PIPE_READ, FS_READ);
    if (!read) return -RUM_ENOMEM;
    struct process_handle *write = new_description(HANDLE_PIPE_WRITE, FS_WRITE);
    if (!write) { (void)release(read); return -RUM_ENOMEM; }
    struct kernel_pipe *pipe = pipe_create();
    if (!pipe) { (void)release(write); (void)release(read); return -RUM_ENOMEM; }
    uint32_t flags = cpu_interrupt_save();
    read->pipe = write->pipe = pipe;
    table->slots[slots[0]] = (struct process_descriptor){ .object = read };
    table->slots[slots[1]] = (struct process_descriptor){ .object = write };
    *reader = slots[0]; *writer = slots[1];
    cpu_interrupt_restore(flags); return 0;
}

static rum_result_t mapping_valid(const struct process_handle *object, unsigned target)
{
    if (target >= RUM_ABI_HANDLE_LIMIT) return -RUM_EINVAL;
    if (target < 3) {
        if (object->kind == HANDLE_DIRECTORY) return -RUM_EISDIR;
        if (!(object->access & (target ? FS_WRITE : FS_READ))) return -RUM_EBADF;
    }
    return 0;
}

rum_result_t process_handle_duplicate(struct process_handles *table, rum_handle_t source, rum_handle_t target, uint32_t flags)
{
    if (flags & ~RUM_DUP_CLOSPAWN) return -RUM_EINVAL;
    struct process_handle *object = process_handle_get(table, source);
    if (!object) return -RUM_EBADF;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (target == RUM_DUP_ANY) {
        target = 3;
        while (target < RUM_ABI_HANDLE_LIMIT && table->slots[target].object) ++target;
        if (target == RUM_ABI_HANDLE_LIMIT) return -RUM_EMFILE;
    }
    rum_result_t error = mapping_valid(object, target);
    if (error) return error;
    if (source == target) { table->slots[target].flags = flags; return (rum_result_t)target; }
    error = retain(object);
    if (error) return error;
    if (table->slots[target].object) error = process_handle_close(table, target);
    if (error) { (void)release(object); return error; }
    uint32_t saved = cpu_interrupt_save();
    table->slots[target] = (struct process_descriptor){ .object = object, .flags = flags };
    cpu_interrupt_restore(saved);
    return (rum_result_t)target;
}

rum_result_t process_handles_prepare_spawn(struct process_handles *destination, const struct process_handles *source,
                                         const struct rum_handle_mapping *mappings, unsigned count, unsigned flags)
{
    if (!destination || destination == source || count > RUM_SPAWN_MAPPING_LIMIT ||
        (count && (!source || !mappings)) || (flags & ~RUM_SPAWN_NO_DEFAULT_STREAMS)) return -RUM_EINVAL;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    uint32_t targets = 0;
    for (unsigned i = 0; i < count; ++i) {
        const struct rum_handle_mapping *map = &mappings[i];
        if (map->reserved[0] || map->reserved[1] || map->target >= RUM_ABI_HANDLE_LIMIT) return -RUM_EINVAL;
        if (targets & (1u << map->target)) return -RUM_EINVAL;
        targets |= 1u << map->target;
        if (map->source >= RUM_ABI_HANDLE_LIMIT || !source->slots[map->source].object) return -RUM_EBADF;
        if (source->slots[map->source].flags & RUM_DUP_CLOSPAWN) return -RUM_EACCES;
        rum_result_t error = mapping_valid(source->slots[map->source].object, map->target);
        if (error) return error;
    }
    *destination = (struct process_handles){0};
    if (!(flags & RUM_SPAWN_NO_DEFAULT_STREAMS)) {
        rum_result_t error = process_handles_initialize(destination);
        if (error) return error;
    }
    for (unsigned i = 0; i < count; ++i) {
        struct process_handle *object = source->slots[mappings[i].source].object;
        rum_result_t error = retain(object);
        if (!error && destination->slots[mappings[i].target].object) {
            error = process_handle_close(destination, mappings[i].target);
            if (error) (void)release(object);
        }
        if (error) { (void)process_handles_destroy(destination); return error; }
        destination->slots[mappings[i].target] = (struct process_descriptor){ .object = object };
    }
    return 0;
}

rum_result_t process_handles_destroy(struct process_handles *table)
{
    if (!table) return -RUM_EINVAL;
    for (unsigned i = 0; i < RUM_ABI_HANDLE_LIMIT; ++i) if (process_handle_get(table, i)) {
        rum_result_t error = process_handle_close(table, i);
        if (error) return error;
    }
    return 0;
}

rum_result_t process_handle_seek(struct process_handle *handle, uint64_t displacement,
                                 unsigned whence, uint64_t *position)
{
    if (!handle) return -RUM_EBADF;
    if (!position || whence > RUM_SEEK_END) return -RUM_EINVAL;
    if (handle->pipe) return -RUM_ESPIPE;
    if (handle->kind == HANDLE_INPUT || handle->kind == HANDLE_OUTPUT) return -RUM_EINVAL;
    if (handle->kind == HANDLE_DIRECTORY && (whence != RUM_SEEK_SET || displacement)) return -RUM_EINVAL;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    uint64_t base = whence == RUM_SEEK_CUR ? handle->offset : 0;
    if (whence == RUM_SEEK_END) {
        struct fs_information info;
        enum fs_error error = fs_stat(handle->reference, &info);
        if (error != FS_OK) return process_fs_error(error);
        base = info.size;
    }
    bool negative = (displacement >> 63) != 0;
    uint64_t magnitude = negative ? ~displacement + 1 : displacement;
    if (negative ? magnitude > base : magnitude > UINT64_MAX - base) return -RUM_EOVERFLOW;
    *position = negative ? base - magnitude : base + magnitude;
    handle->offset = *position;
    return 0;
}

static rum_result_t transfer(struct process_handle *handle, void *buffer, size_t bytes, bool write)
{
    if (!handle || !(handle->access & (write ? FS_WRITE : FS_READ))) return -RUM_EBADF;
    if (handle->kind == HANDLE_DIRECTORY) return -RUM_EISDIR;
    if (handle->pipe) return write ? pipe_write(handle->pipe, buffer, bytes) : pipe_read(handle->pipe, buffer, bytes);
    if (handle->kind != HANDLE_FILE || (bytes && !buffer)) return -RUM_EINVAL;
    if (bytes > INT32_MAX || (!(write && handle->append) && bytes > UINT64_MAX - handle->offset)) return -RUM_EOVERFLOW;
    if (!bytes) return 0;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (handle->pending_error) {
        rum_result_t error = handle->pending_error; handle->pending_error = 0; return error;
    }
    struct fs_io_result result = write && handle->append ? fs_append(handle->reference, buffer, bytes, &handle->offset) :
                                write ? fs_write(handle->reference, handle->offset, buffer, bytes) :
                                       fs_read(handle->reference, handle->offset, buffer, bytes);
    if (!(write && handle->append)) handle->offset += result.transferred;
    rum_result_t error = process_fs_error(result.error);
    /* A committed prefix wins over the error; report the error on the next
       nonempty transfer so callers never retry bytes already committed. */
    if (result.transferred && error) handle->pending_error = error;
    return result.transferred ? (rum_result_t)result.transferred : error;
}

rum_result_t process_handle_read(struct process_handle *handle, void *buffer, size_t bytes)
{
    return transfer(handle, buffer, bytes, false);
}
rum_result_t process_handle_write(struct process_handle *handle, const void *buffer, size_t bytes)
{
    return transfer(handle, (void *)buffer, bytes, true);
}
rum_result_t process_handle_readdir(struct process_handle *handle, struct fs_entry *entry)
{
    if (handle && handle->pipe) return -RUM_ENOTDIR;
    if (!handle || !(handle->access & FS_READ)) return -RUM_EBADF;
    if (handle->kind != HANDLE_DIRECTORY) return -RUM_ENOTDIR;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    enum fs_error error = fs_readdir(handle->reference, &handle->offset, entry);
    return error == FS_OK ? 1 : process_fs_error(error);
}
rum_result_t process_handle_flush(struct process_handle *handle)
{
    if (handle && handle->pipe) return -RUM_EINVAL;
    if (!handle || !handle->reference) return -RUM_EBADF;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    return process_fs_error(fs_flush(handle->reference));
}
