#ifndef RUM_FS_TYPES_H
#define RUM_FS_TYPES_H
#include <rum/block.h>

enum fs_error {
    FS_OK, FS_END, FS_INVALID, FS_UNAVAILABLE, FS_NOT_FOUND, FS_NOT_DIRECTORY,
    FS_IS_DIRECTORY, FS_EXISTS, FS_NOT_EMPTY, FS_PATH_TOO_LONG, FS_NAME_TOO_LONG,
    FS_TOO_DEEP, FS_MOUNT_ESCAPE, FS_RANGE, FS_ACCESS, FS_READ_ONLY, FS_BUSY,
    FS_NO_MEMORY, FS_NO_SPACE, FS_UNSUPPORTED, FS_IO_ERROR, FS_TIMEOUT, FS_DEVICE_FAULT
};
enum fs_kind { FS_FILE, FS_DIRECTORY };
enum fs_naming { FS_NAMES_RAM, FS_NAMES_FAT83 };
enum fs_mount_id { FS_MOUNT_RAM, FS_MOUNT_DISK, FS_MOUNT_SYSTEM, FS_MOUNT_COUNT };
enum fs_access { FS_READ = 1, FS_WRITE = 2 };

struct fs_node { uint64_t id, size; enum fs_kind kind; };
struct fs_io_result {
    enum fs_error error;
    size_t transferred;
    uint8_t device_status, device_error;
};
const char *fs_error_name(enum fs_error error);
enum fs_error fs_error_from_block(enum block_error error);
#endif
