#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/ramfs.h>
#include "fat16-checks.h"

unsigned char fat16_test_data[FAT16_TEST_BYTES];
char fat16_test_listing[FAT16_TEST_LISTING_CAPACITY];
uint32_t fat16_test_listing_length;
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void append(const char *text)
{
    while (*text) {
        fat16_check(fat16_test_listing_length + 1 < FAT16_TEST_LISTING_CAPACITY, "listing capacity");
        fat16_test_listing[fat16_test_listing_length++] = *text++;
    }
    fat16_test_listing[fat16_test_listing_length] = 0;
}
static void number(uint64_t value)
{
    char digits[21]; unsigned length = 0;
    do { digits[length++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (length) { char text[2] = {digits[--length], 0}; append(text); }
}
static void listing(const char *path)
{
    fs_reference directory;
    fat16_check(fs_open(NULL, path, FS_READ, &directory) == FS_OK, "open listed directory");
    uint64_t cursor = 0; struct fs_entry entry; enum fs_error error;
    while ((error = fs_readdir(directory, &cursor, &entry)) == FS_OK) {
        if (path[5]) append(path + 5);
        append("/"); append(entry.name); append(entry.kind == FS_DIRECTORY ? "\tD\t" : "\tF\t");
        number(entry.size); append("\n");
    }
    fat16_check(error == FS_END && fs_close(directory) == FS_OK, "listed directory EOF and close");
}
static unsigned char pattern(size_t index)
{
    return (unsigned char)((index * 29 + (index >> 8) * 11) ^ 0xa5);
}
static void valid_volume(void)
{
    fs_reference file, empty;
    struct fs_information info;
    fat16_check(fs_open(NULL, "/disk/binary.bin", FS_READ, &file) == FS_OK &&
        fs_stat(file, &info) == FS_OK && info.size == FAT16_TEST_BYTES, "open exact binary file");
    struct fs_io_result result = fs_read(file, 0, fat16_test_data, sizeof fat16_test_data);
    fat16_check(result.error == FS_OK && result.transferred == FAT16_TEST_BYTES, "read fragmented binary file");
    for (size_t i = 0; i < FAT16_TEST_BYTES; ++i) fat16_check(fat16_test_data[i] == pattern(i), "binary bytes");
    unsigned char buffer[257];
    for (size_t offset = 0; offset < FAT16_TEST_BYTES; offset += 137) {
        result = fs_read(file, offset, buffer, sizeof buffer);
        size_t expected = FAT16_TEST_BYTES - offset;
        if (expected > sizeof buffer) expected = sizeof buffer;
        fat16_check(result.error == FS_OK && result.transferred == expected &&
                     !memcmp(buffer, fat16_test_data + offset, expected), "sequential short reads");
    }
    const uint64_t offsets[] = {0, 1, 511, 512, 1023, 1024, 2047, 8191, 10036, 10037, UINT64_MAX - 257};
    for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; ++i) {
        result = fs_read(file, offsets[i], buffer, sizeof buffer);
        size_t expected = offsets[i] >= FAT16_TEST_BYTES ? 0 : FAT16_TEST_BYTES - (size_t)offsets[i];
        if (expected > sizeof buffer) expected = sizeof buffer;
        fat16_check(result.error == FS_OK && result.transferred == expected &&
            (!expected || !memcmp(buffer, fat16_test_data + (size_t)offsets[i], expected)), "random reads and EOF");
    }
    fat16_check(fs_read(file, UINT64_MAX, buffer, 1).error == FS_RANGE &&
                 fs_write(file, 0, "x", 1).error == FS_ACCESS && fs_flush(file) == FS_OK,
                 "overflow access and read-only flush");
    fat16_check(fs_open(NULL, "/disk/EMPTY.BIN", FS_READ, &empty) == FS_OK &&
                 fs_read(empty, 0, buffer, sizeof buffer).transferred == 0 && fs_close(empty) == FS_OK,
                 "empty file uses no cluster");
    fs_reference rejected;
    fat16_check(fs_open(NULL, "/disk/BINARY.BIN", FS_WRITE, &rejected) == FS_READ_ONLY &&
        fs_replace(NULL, "/disk/NEW.TXT", "x", 1) == FS_READ_ONLY &&
        fs_remove(NULL, "/disk/BINARY.BIN") == FS_READ_ONLY &&
        fs_mkdir(NULL, "/disk/NEW") == FS_READ_ONLY && fat16_unmount() == FS_BUSY,
        "read-only backend and open-object mount pin");
    struct fs_context cwd = {0};
    fat16_check(fs_context_initialize(&cwd) == FS_OK && fs_context_chdir(&cwd, "/disk/docs/nest") == FS_OK &&
        fs_open(&cwd, "./NOTE.TXT", FS_READ, &empty) == FS_OK, "nested relative read");
    result = fs_read(empty, 0, buffer, sizeof buffer);
    static const char note[] = "An island within an island.\n";
    fat16_check(result.error == FS_OK && result.transferred == sizeof note - 1 &&
        !memcmp(buffer, note, sizeof note - 1) && fs_close(empty) == FS_OK, "nested file exact bytes");
    fat16_check(fs_context_chdir(&cwd, "../../../") == FS_MOUNT_ESCAPE &&
        fs_stat_path(NULL, "/disk/MISSING", &info) == FS_NOT_FOUND &&
        fs_stat_path(NULL, "/disk/BINARY.BIN/.", &info) == FS_NOT_DIRECTORY &&
        fs_context_chdir(&cwd, "..") == FS_OK && fs_context_destroy(&cwd) == FS_OK,
        "directory traversal and boundary errors");
    listing("/disk"); listing("/disk/DOCS"); listing("/disk/DOCS/NEST");
    fat16_check(fs_close(file) == FS_OK, "close binary reference");
}
void fat16_checks(struct block_device *device, const char *phase, const char *path)
{
    struct heap_statistics baseline = heap_stats();
    struct fs_statistics owners = fs_stats();
    enum fs_error mounted = fat16_mount_read_only(device);
    if (equal(phase, "mount")) {
        fat16_check(mounted != FS_OK && !fat16_info().mounted, "reject malformed volume geometry or FAT");
        fat16_report("mount", mounted);
    } else {
        if (mounted != FS_OK) fat16_report("unexpected-mount-error", mounted);
        fat16_check(mounted == FS_OK && fat16_info().mounted, "mount host FAT16 image");
        if (equal(phase, "open")) {
            fs_reference file;
            enum fs_error error = fs_open(NULL, path, FS_READ, &file);
            fat16_check(error != FS_OK && !file, "reject malformed chain or directory entry");
            fat16_report("open", error);
        } else { valid_volume(); fat16_report("valid", FS_OK); }
        fat16_check(fat16_unmount() == FS_OK && !fat16_info().mounted, "release mounted FAT metadata");
    }
    fat16_check(heap_stats().used_bytes == baseline.used_bytes && heap_stats().allocations == baseline.allocations &&
        fs_stats().objects == owners.objects && fs_stats().references == owners.references &&
        fs_stats().mounts == owners.mounts, "FAT operation leaves ownership baseline");
    fs_reference ram; unsigned char data[6];
    fat16_check(fs_open(NULL, "/ram.txt", FS_READ, &ram) == FS_OK &&
        fs_read(ram, 0, data, sizeof data).transferred == sizeof data && !memcmp(data, "island", sizeof data) &&
        fs_close(ram) == FS_OK, "RAM stays usable after disk rejection or cleanup");
}
