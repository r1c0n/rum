#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/ramfs.h>
#include "fat16-write-checks.h"

static unsigned char expected[FAT16_WRITE_TEST_BYTES], received[FAT16_WRITE_TEST_BYTES];
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void pattern(void)
{
    for (size_t i = 0; i < sizeof expected; ++i) expected[i] = (unsigned char)((i * 37) ^ (i >> 7) ^ 0x59);
}
static void verify(const char *path, size_t size)
{
    fs_reference file; struct fs_information info;
    fat16_write_check(fs_open(NULL, path, FS_READ, &file) == FS_OK &&
        fs_stat(file, &info) == FS_OK && info.size == size, "file size");
    memset(received, 0xcc, sizeof received);
    struct fs_io_result result = fs_read(file, 0, received, sizeof received);
    fat16_write_check(result.error == FS_OK && result.transferred == size &&
        !memcmp(expected, received, size) && fs_close(file) == FS_OK, "exact file bytes");
    if (size < sizeof received) fat16_write_check(received[size] == 0xcc, "read canary");
}
enum fs_error fat16_write_operation(const char *name, size_t *transferred)
{
    pattern(); *transferred = 0;
    if (equal(name, "create") || equal(name, "reject") || equal(name, "full") || equal(name, "oom") ||
        equal(name, "readfail") || equal(name, "endclear") || equal(name, "orphan"))
        return fs_replace(NULL, "/disk/NEW.BIN", expected, 4097);
    if (equal(name, "attribute")) return fs_replace(NULL, "/disk/TARGET.BIN", expected, 4097);
    if (equal(name, "hidden")) return fs_remove(NULL, "/disk/VOID");
    if (equal(name, "lfn")) return fs_remove(NULL, "/disk/TARGET.BIN");
    if (equal(name, "replace")) return fs_replace(NULL, "/disk/TARGET.BIN", expected, 4097);
    if (equal(name, "delete")) return fs_remove(NULL, "/disk/TARGET.BIN");
    if (equal(name, "mkdir")) return fs_mkdir(NULL, "/disk/NEW");
    if (equal(name, "rmdir")) return fs_remove(NULL, "/disk/VOID");
    if (equal(name, "growdir")) return fs_replace(NULL, "/disk/FULL/NEW.BIN", expected, 4097);
    if (equal(name, "growmkdir")) return fs_mkdir(NULL, "/disk/FULL/NEW");
    if (equal(name, "fullgrowdir")) return fs_replace(NULL, "/disk/FULL/NEW.BIN", expected, 4097);
    if (equal(name, "fullgrowmkdir")) return fs_mkdir(NULL, "/disk/FULL/NEW");
    fs_reference file;
    enum fs_error error = fs_open(NULL, "/disk/TARGET.BIN", FS_READ | FS_WRITE, &file);
    if (error != FS_OK) return error;
    if (equal(name, "write") || equal(name, "fullwrite")) {
        struct fs_io_result result = fs_write(file, 1537, expected, 1025);
        error = result.error; *transferred = result.transferred;
        if (error != FS_OK && !equal(name, "fullwrite")) fat16_write_check(result.device_status == 0x51 && result.device_error == 4,
                                             "write diagnostics");
    } else error = fs_truncate(file, equal(name, "zero") ? 0 :
        equal(name, "shrink") || equal(name, "fullshrink") ? 513 : 4097);
    fat16_write_check(fs_close(file) == FS_OK, "failed or successful write closes its handle");
    return error;
}
void fat16_write_checks(struct block_device *device)
{
    struct heap_statistics heap = heap_stats(); struct fs_statistics owners = fs_stats();
    pattern();
    /* Resize also belongs to the common RAM API and keeps its 64 KiB limit. */
    fs_reference ram; struct fs_information ram_info;
    fat16_write_check(fs_replace(NULL, "/resize.txt", "rum", 3) == FS_OK &&
        fs_open(NULL, "/resize.txt", FS_READ | FS_WRITE, &ram) == FS_OK &&
        fs_truncate(ram, RAMFS_FILE_LIMIT) == FS_OK && fs_stat(ram, &ram_info) == FS_OK &&
        ram_info.size == RAMFS_FILE_LIMIT && fs_truncate(ram, RAMFS_FILE_LIMIT + 1) == FS_RANGE,
        "RAM resize and limit");
    fat16_write_check(fs_read(ram, 0, received, sizeof received).transferred == sizeof received &&
        !memcmp(received, "rum", 3), "RAM resize preserves prefix");
    for (size_t i = 3; i < sizeof received; ++i) fat16_write_check(!received[i], "RAM extension zero fill");
    fat16_write_check(fs_truncate(ram, 1) == FS_OK && fs_truncate(ram, 0) == FS_OK &&
        fs_read(ram, 0, received, 1).transferred == 0 && fs_close(ram) == FS_OK &&
        fs_remove(NULL, "/resize.txt") == FS_OK, "RAM shrink and cleanup");
    fat16_write_check(fat16_mount(device) == FS_OK && fat16_info().writable, "mount writable volume");
    fat16_write_check(fs_replace(NULL, "/disk/EMPTY.BIN", NULL, 0) == FS_OK, "create empty file");
    verify("/disk/EMPTY.BIN", 0);
    const size_t sizes[] = {1, 511, 512, 513, 1023, 1024, 1025, 4095, 4096, 4097, 16385};
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        fat16_write_check(fs_replace(NULL, "/disk/BOUND.BIN", expected, sizes[i]) == FS_OK, "boundary replacement");
        verify("/disk/BOUND.BIN", sizes[i]);
    }
    fs_reference file, alias; struct fs_information before, after;
    fat16_write_check(fs_open(NULL, "/disk/BOUND.BIN", FS_READ | FS_WRITE, &file) == FS_OK &&
        fs_open(NULL, "/disk/./bound.bin", FS_READ, &alias) == FS_OK && fs_stat(file, &before) == FS_OK,
        "open retained aliases");
    fat16_write_check(fs_replace(NULL, "/disk/BOUND.BIN", "x", 1) == FS_BUSY &&
        fs_remove(NULL, "/disk/BOUND.BIN") == FS_BUSY && fs_truncate(alias, 1) == FS_ACCESS &&
        fs_truncate(file, UINT64_MAX) == FS_RANGE && fs_write(file, UINT64_MAX, "x", 1).error == FS_RANGE &&
        fs_write(file, 17000, "x", 1).error == FS_RANGE, "retention access and range checks");
    fat16_write_check(fs_write(file, 511, "rum", 3).error == FS_OK, "overwrite crosses sector boundary");
    memcpy(expected + 511, "rum", 3); verify("/disk/BOUND.BIN", 16385);
    fat16_write_check(fs_write(file, 16385, "end", 3).transferred == 3, "extend through handle");
    memcpy(expected + 16385, "end", 3); verify("/disk/BOUND.BIN", 16388);
    fat16_write_check(fs_stat(alias, &after) == FS_OK && before.identity.object == after.identity.object &&
        after.size == 16388 && fs_truncate(file, 513) == FS_OK, "aliases observe stable identity and truncation");
    verify("/disk/BOUND.BIN", 513);
    fat16_write_check(fs_truncate(file, 4097) == FS_OK, "zero-filled extension");
    memset(expected + 513, 0, 4097 - 513); verify("/disk/BOUND.BIN", 4097);
    fat16_write_check(fs_truncate(file, 0) == FS_OK && fs_truncate(file, 0) == FS_OK &&
        fs_flush(file) == FS_OK && fs_close(file) == FS_OK && fs_close(alias) == FS_OK, "truncate empty and flush");
    verify("/disk/BOUND.BIN", 0);
    fat16_write_check(fs_remove(NULL, "/disk/BOUND.BIN") == FS_OK, "delete empty file");
    pattern();
    for (unsigned i = 0; i < 20; ++i) {
        fat16_write_check(fs_replace(NULL, "/disk/REUSE.BIN", expected, 4097) == FS_OK &&
            fs_remove(NULL, "/disk/REUSE.BIN") == FS_OK, "reuse file slot and clusters");
    }
    fat16_write_check(fs_mkdir(NULL, "/disk/TREE") == FS_OK && fs_mkdir(NULL, "/disk/TREE/NEST") == FS_OK &&
        fs_replace(NULL, "/disk/TREE/NEST/NOTE.TXT", "island\n", 7) == FS_OK &&
        fs_remove(NULL, "/disk/TREE") == FS_NOT_EMPTY && fs_remove(NULL, "/disk/TREE/NEST") == FS_NOT_EMPTY,
        "nested mkdir and nonempty removal");
    struct fs_context cwd = {0};
    fat16_write_check(fs_context_initialize(&cwd) == FS_OK && fs_context_chdir(&cwd, "/disk/TREE/NEST") == FS_OK &&
        fs_remove(NULL, "/disk/TREE/NEST/NOTE.TXT") == FS_OK && fs_remove(NULL, "/disk/TREE/NEST") == FS_BUSY &&
        fs_context_destroy(&cwd) == FS_OK && fs_remove(NULL, "/disk/TREE/NEST") == FS_OK &&
        fs_remove(NULL, "/disk/TREE") == FS_OK, "working directory pin and rmdir");
    /* Force growth beyond one cluster, then reuse its deleted slots. */
    fat16_write_check(fs_mkdir(NULL, "/disk/GROW") == FS_OK, "growth directory");
    unsigned count = fat16_info().cluster_bytes / 32 + 3;
    for (unsigned i = 0; i < count; ++i) {
        char path[] = "/disk/GROW/F0000.TXT";
        for (unsigned n = i, p = 15; p >= 12; --p, n /= 10) path[p] = (char)('0' + n % 10);
        fat16_write_check(fs_replace(NULL, path, "g", 1) == FS_OK, "grow directory slots");
    }
    fat16_write_check(fs_remove(NULL, "/disk/GROW/F0000.TXT") == FS_OK &&
        fs_replace(NULL, "/disk/GROW/F0000.TXT", "r", 1) == FS_OK, "reuse deleted directory slot");
    /* Leave persistent output for mtools and the independent host audit. */
    fat16_write_check(fs_replace(NULL, "/disk/RESULT.BIN", expected, 16385) == FS_OK &&
        fs_mkdir(NULL, "/disk/SAVED") == FS_OK && fs_replace(NULL, "/disk/SAVED/NOTE.TXT", "rum writes FAT16\n", 17) == FS_OK &&
        fat16_unmount() == FS_OK && fat16_mount(device) == FS_OK, "persistent output and remount");
    verify("/disk/RESULT.BIN", 16385);
    fat16_write_check(fat16_unmount() == FS_OK && heap_stats().used_bytes == heap.used_bytes &&
        heap_stats().allocations == heap.allocations && fs_stats().objects == owners.objects &&
        fs_stats().references == owners.references && fs_stats().mounts == owners.mounts, "write ownership baseline");
}
void fat16_write_verify(struct block_device *device)
{
    struct heap_statistics heap = heap_stats();
    pattern();
    fat16_write_check(fat16_mount_read_only(device) == FS_OK, "mount saved volume after reboot");
    verify("/disk/RESULT.BIN", 16385);
    fs_reference file;
    fat16_write_check(fs_open(NULL, "/disk/SAVED/NOTE.TXT", FS_READ, &file) == FS_OK &&
        fs_read(file, 0, received, sizeof received).transferred == 17 &&
        !memcmp(received, "rum writes FAT16\n", 17) && fs_close(file) == FS_OK && fat16_unmount() == FS_OK &&
        heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes,
        "reboot file bytes and cleanup");
}
