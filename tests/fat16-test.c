#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rum/heap.h>
#include <rum/ramfs.h>
#include "fat16-checks.h"
#include "page-backend.h"

static FILE *image;
static unsigned reads, writes;
static int failure_after = -1;
static enum block_error failure = BLOCK_TIMEOUT;
static struct block_result read_image(struct block_device *device, uint64_t lba, uint64_t count, void *buffer)
{
    ++reads;
    assert(lba < device->sector_count && count <= device->sector_count - lba);
    if (!failure_after) return (struct block_result){ .error = failure, .status = 0x51, .device_error = 4 };
    if (failure_after > 0) --failure_after;
    assert(!fseek(image, (long)(lba * 512), SEEK_SET));
    assert(fread(buffer, 512, (size_t)count, image) == count);
    return (struct block_result){ .completed = count };
}
static struct block_result write_image(struct block_device *device, uint64_t lba, uint64_t count, const void *buffer)
{
    (void)device; (void)lba; (void)count; (void)buffer; ++writes;
    return block_result(BLOCK_IO_ERROR);
}
static const struct block_operations operations = { .read = read_image, .write = write_image };

void fat16_check(bool condition, const char *name)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); abort(); }
}
void fat16_report(const char *phase, enum fs_error error) { printf("rum_fat_result: %s %s\n", phase, fs_error_name(error)); }

static void faults(struct block_device *device)
{
    struct heap_statistics baseline = heap_stats();
    failure_after = 0;
    for (unsigned i = 0; i < 3; ++i) {
        failure = i == 0 ? BLOCK_TIMEOUT : i == 1 ? BLOCK_DEVICE_FAULT : BLOCK_IO_ERROR;
        assert(fat16_mount(device) == fs_error_from_block(failure) && !fat16_info().mounted &&
               heap_stats().used_bytes == baseline.used_bytes && heap_stats().allocations == baseline.allocations);
    }
    /* First FAT read succeeds, then the second copy fails: release the snapshot. */
    failure_after = 2; failure = BLOCK_IO_ERROR;
    assert(fat16_mount(device) == FS_IO_ERROR && heap_stats().allocations == baseline.allocations);
    failure_after = -1;
    assert(ramfs_put("disk", "old", 3) && fat16_mount(device) == FS_EXISTS &&
           heap_stats().allocations == baseline.allocations + 2 && ramfs_remove("disk"));
    assert(fat16_mount(device) == FS_OK);
    fs_reference file;
    assert(fs_open(NULL, "/disk/BINARY.BIN", FS_READ, &file) == FS_OK);
    unsigned char buffer[1024]; memset(buffer, 0xcc, sizeof buffer);
    failure_after = 1; failure = BLOCK_TIMEOUT;
    struct fs_io_result result = fs_read(file, 0, buffer, sizeof buffer);
    assert(result.error == FS_TIMEOUT && result.transferred == 512 && result.device_status == 0x51 && result.device_error == 4);
    for (unsigned i = 0; i < 512; ++i) {
        assert(buffer[i] == (unsigned char)((i * 29 + (i >> 8) * 11) ^ 0xa5));
        assert(buffer[i + 512] == 0xcc);
    }
    failure_after = -1;
    assert(fs_read(file, 512, buffer, 512).transferred == 512 && fs_close(file) == FS_OK && fat16_unmount() == FS_OK);
    assert(fat16_mount(device) == FS_OK && fs_unmount_disk() == FS_OK && !fat16_info().mounted &&
           fat16_unmount() == FS_UNAVAILABLE && fat16_mount(device) == FS_OK && fat16_unmount() == FS_OK);
    assert(!writes && heap_stats().allocations == baseline.allocations && heap_stats().used_bytes == baseline.used_bytes);
}
int main(int argc, char **argv)
{
    assert(argc == 5);
    image = fopen(argv[1], "rb"); assert(image && !fseek(image, 0, SEEK_END));
    long length = ftell(image); assert(length > 0 && length % 512 == 0);
    struct block_device device = { .sector_count = (uint64_t)length / 512, .sector_size = 512,
                                  .online = true, .operations = &operations };
    test_page_budget(-1);
    assert(heap_initialize() && ramfs_initialize() && fs_initialize() && ramfs_put("ram.txt", "island", 6));
    if (!strcmp(argv[2], "valid")) {
        struct heap_statistics baseline = heap_stats(); test_page_budget(0);
        assert(fat16_mount(&device) == FS_NO_MEMORY && !fat16_info().mounted &&
               heap_stats().used_bytes == baseline.used_bytes && heap_stats().allocations == baseline.allocations);
        test_page_budget(-1);
    }
    fat16_checks(&device, argv[2], argv[3]);
    if (!strcmp(argv[2], "valid")) faults(&device);
    assert(!writes);
    if (argv[4][0]) {
        FILE *listing = fopen(argv[4], "wb"); assert(listing);
        assert(fwrite(fat16_test_listing, 1, fat16_test_listing_length, listing) == fat16_test_listing_length);
        assert(!fclose(listing));
    }
    assert(reads && !fclose(image));
    puts("PASS: FAT16 bytes, directories, validated bounds and chains, read-only policy, failure cleanup");
    return 0;
}
