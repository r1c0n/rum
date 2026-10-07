#include <rum/abi/error.h>
#include <rum/handles.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>

_Static_assert(RUM_OPEN_READ == FS_READ && RUM_OPEN_WRITE == FS_WRITE, "access bits");

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
    case FS_TIMEOUT: return -RUM_ETIMEDOUT;
    case FS_IO_ERROR: case FS_DEVICE_FAULT: return -RUM_EIO;
    }
    return -RUM_EIO;
}

void process_handles_initialize(struct process_handles *table)
{
    *table = (struct process_handles){0};
    table->slots[0] = (struct process_handle){ .kind = HANDLE_INPUT, .access = FS_READ };
    table->slots[1] = table->slots[2] = (struct process_handle){ .kind = HANDLE_OUTPUT, .access = FS_WRITE };
}

unsigned process_handles_count(const struct process_handles *table)
{
    unsigned count = 0;
    if (table) for (unsigned i = 0; i < RUM_ABI_HANDLE_LIMIT; ++i) count += table->slots[i].kind != HANDLE_UNUSED;
    return count;
}

struct process_handle *process_handle_get(struct process_handles *table, rum_handle_t handle)
{
    return table && handle < RUM_ABI_HANDLE_LIMIT && table->slots[handle].kind != HANDLE_UNUSED ? &table->slots[handle] : NULL;
}

rum_result_t process_handle_open(struct process_handles *table, const struct fs_context *context,
                                 const char *path, unsigned access, unsigned flags)
{
    if (!table || !path || !access || (access & ~(FS_READ | FS_WRITE)) ||
        (flags & ~RUM_OPEN_DIRECTORY) || ((flags & RUM_OPEN_DIRECTORY) && access != FS_READ)) return -RUM_EINVAL;
    unsigned slot = 3;
    while (slot < RUM_ABI_HANDLE_LIMIT && table->slots[slot].kind != HANDLE_UNUSED) ++slot;
    if (slot == RUM_ABI_HANDLE_LIMIT) return -RUM_EMFILE;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    fs_reference reference = 0;
    enum fs_error error = fs_open(context, path, access, &reference);
    if (error != FS_OK) return process_fs_error(error);
    struct fs_information info;
    error = fs_stat(reference, &info);
    if (error == FS_OK && (flags & RUM_OPEN_DIRECTORY) && info.kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
    if (error != FS_OK) { (void)fs_close(reference); return process_fs_error(error); }
    uint32_t saved = cpu_interrupt_save();
    table->slots[slot] = (struct process_handle){ .reference = reference, .access = access,
        .kind = info.kind == FS_DIRECTORY ? HANDLE_DIRECTORY : HANDLE_FILE };
    cpu_interrupt_restore(saved);
    return (rum_result_t)slot;
}

rum_result_t process_handle_close(struct process_handles *table, rum_handle_t id)
{
    struct process_handle *handle = process_handle_get(table, id);
    if (!handle) return -RUM_EBADF;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (handle->reference) {
        enum fs_error error = fs_close(handle->reference);
        if (error != FS_OK) return process_fs_error(error);
    }
    uint32_t saved = cpu_interrupt_save();
    *handle = (struct process_handle){0};
    cpu_interrupt_restore(saved);
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
    if (handle->kind != HANDLE_FILE || (bytes && !buffer)) return -RUM_EINVAL;
    if (bytes > INT32_MAX || bytes > UINT64_MAX - handle->offset) return -RUM_EOVERFLOW;
    if (!bytes) return 0;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    if (handle->pending_error) {
        rum_result_t error = handle->pending_error; handle->pending_error = 0; return error;
    }
    struct fs_io_result result = write ? fs_write(handle->reference, handle->offset, buffer, bytes) :
                                       fs_read(handle->reference, handle->offset, buffer, bytes);
    handle->offset += result.transferred;
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
    if (!handle || !(handle->access & FS_READ)) return -RUM_EBADF;
    if (handle->kind != HANDLE_DIRECTORY) return -RUM_ENOTDIR;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    enum fs_error error = fs_readdir(handle->reference, &handle->offset, entry);
    return error == FS_OK ? 1 : process_fs_error(error);
}
rum_result_t process_handle_flush(struct process_handle *handle)
{
    if (!handle || !handle->reference) return -RUM_EBADF;
    if (!process_filesystem_context()) return -RUM_EBUSY;
    return process_fs_error(fs_flush(handle->reference));
}
