#include <rum/fs_types.h>

const char *fs_error_name(enum fs_error error)
{
    static const char *const names[] = {
        "ok", "end of directory", "invalid request", "unavailable filesystem", "not found",
        "not a directory", "is a directory", "already exists", "directory not empty",
        "path too long", "name too long", "path too deep", "mount traversal",
        "offset range", "access mode", "read only", "busy", "out of memory", "out of space",
        "unsupported operation", "I/O error", "timeout", "device fault"
    };
    return (unsigned)error < sizeof(names) / sizeof(names[0]) ? names[error] : "unknown error";
}

enum fs_error fs_error_from_block(enum block_error error)
{
    switch (error) {
    case BLOCK_OK: return FS_OK;
    case BLOCK_INVALID: return FS_INVALID;
    case BLOCK_RANGE: return FS_RANGE;
    case BLOCK_NO_DEVICE: return FS_UNAVAILABLE;
    case BLOCK_READ_ONLY: return FS_READ_ONLY;
    case BLOCK_BUSY: return FS_BUSY;
    case BLOCK_TIMEOUT: return FS_TIMEOUT;
    case BLOCK_DEVICE_FAULT: return FS_DEVICE_FAULT;
    case BLOCK_UNSUPPORTED: return FS_UNSUPPORTED;
    case BLOCK_IO_ERROR: return FS_IO_ERROR;
    }
    return FS_IO_ERROR;
}
