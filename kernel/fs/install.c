#include <rum/fs_policy.h>
#include <rum/path.h>
#include <rum/abi/elf.h>

enum fs_error fs_install_system_file(const char *source, const char *destination, uint64_t *copied)
{
    if (!copied) return FS_INVALID;
    *copied = 0;
    if (!fs_system_write_allowed()) return FS_ACCESS;
    struct fs_path target;
    enum fs_error error = fs_path_parse("/", destination, &target);
    if (error != FS_OK) return error;
    if (target.depth < 2 || target.canonical[1] != 'R' || target.canonical[2] != 'U' ||
        target.canonical[3] != 'M' || target.canonical[4] != '/' || target.input.directory) return FS_INVALID;
    fs_reference input = 0, output = 0;
    error = fs_open(NULL, source, FS_READ, &input);
    if (error != FS_OK) return error;
    struct fs_information info = {0}, existing = {0};
    error = fs_stat(input, &info);
    if (error == FS_OK && info.kind != FS_FILE) error = FS_IS_DIRECTORY;
    if (error == FS_OK && info.size > RUM_ELF_IMAGE_LIMIT) error = FS_RANGE;
    if (error == FS_OK) {
        enum fs_error found = fs_stat_path(NULL, destination, &existing);
        if (found == FS_OK && existing.identity.mount == info.identity.mount &&
            existing.identity.generation == info.identity.generation && existing.identity.object == info.identity.object) error = FS_INVALID;
        else if (found != FS_OK && found != FS_NOT_FOUND) error = found;
    }
    if (error == FS_OK) {
        error = fs_mkdir(NULL, "/RUM");
        if (error == FS_EXISTS) error = FS_OK;
    }
    if (error == FS_OK) error = fs_open_flags(NULL, destination, FS_WRITE, FS_OPEN_CREATE | FS_OPEN_TRUNCATE, &output);
    unsigned char bytes[512];
    while (error == FS_OK && *copied < info.size) {
        size_t count = info.size - *copied < sizeof bytes ? (size_t)(info.size - *copied) : sizeof bytes;
        struct fs_io_result read = fs_read(input, *copied, bytes, count);
        if (read.error != FS_OK || read.transferred != count) { error = read.error != FS_OK ? read.error : FS_IO_ERROR; break; }
        struct fs_io_result write = fs_write(output, *copied, bytes, count);
        *copied += write.transferred;
        if (write.error != FS_OK || write.transferred != count) error = write.error != FS_OK ? write.error : FS_IO_ERROR;
    }
    if (error == FS_OK) error = fs_flush(output);
    if (output) { enum fs_error closed = fs_close(output); if (error == FS_OK) error = closed; }
    enum fs_error closed = fs_close(input);
    return error == FS_OK ? closed : error;
}
