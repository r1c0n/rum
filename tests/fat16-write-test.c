#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rum/heap.h>
#include <rum/ramfs.h>
#include "fat16-write-checks.h"
#include "page-backend.h"

static FILE *image, *trace;
static unsigned events, writes;
static int fail_at = -1, tear;
static uint64_t volume_sectors;
static bool read_failure;
static struct block_result read_image(struct block_device *device, uint64_t lba, uint64_t count, void *data)
{
    assert(lba < device->sector_count && count <= device->sector_count - lba);
    if (read_failure && writes) return (struct block_result){ .error = BLOCK_IO_ERROR, .status = 0x51, .device_error = 4 };
    assert(!fseek(image, (long)(lba * 512), SEEK_SET) && fread(data, 512, (size_t)count, image) == count);
    return (struct block_result){ .completed = count };
}
static struct block_result write_image(struct block_device *device, uint64_t lba, uint64_t count, const void *data)
{
    assert(lba < volume_sectors && count == 1 && lba < device->sector_count);
    ++writes;
    if (trace) fprintf(trace, "W %llu\n", (unsigned long long)lba);
    bool fail = (int)events++ == fail_at;
    size_t bytes = fail ? (tear <= 512 ? (size_t)tear : 0) : 512;
    assert(!fseek(image, (long)(lba * 512), SEEK_SET) && fwrite(data, 1, bytes, image) == bytes);
    if (fail) return (struct block_result){ .error = tear == 1024 ? BLOCK_TIMEOUT :
        tear == 2048 ? BLOCK_DEVICE_FAULT : BLOCK_IO_ERROR, .status = 0x51, .device_error = 4 };
    return (struct block_result){ .completed = 1 };
}
static struct block_result flush_image(struct block_device *device)
{
    (void)device;
    if (trace) fputs("F\n", trace);
    assert(!fflush(image));
    if ((int)events++ == fail_at) return (struct block_result){ .error = tear == 1024 ? BLOCK_TIMEOUT :
        tear == 2048 ? BLOCK_DEVICE_FAULT : BLOCK_IO_ERROR, .status = 0x51, .device_error = 4 };
    return block_result(BLOCK_OK);
}
static const struct block_operations operations = { read_image, write_image, flush_image };
void fat16_write_check(bool condition, const char *name)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); abort(); }
}
int main(int argc, char **argv)
{
    assert(argc == 5 || argc == 6);
    if (argc == 6) { trace = fopen(argv[5], "w"); assert(trace); }
    image = fopen(argv[1], "r+b"); assert(image && !fseek(image, 0, SEEK_END));
    long size = ftell(image); assert(size > 0 && size % 512 == 0);
    unsigned char boot[512]; assert(!fseek(image, 0, SEEK_SET) && fread(boot, 1, sizeof boot, image) == sizeof boot);
    volume_sectors = boot[19] | (uint32_t)boot[20] << 8;
    if (!volume_sectors) volume_sectors = boot[32] | (uint32_t)boot[33] << 8 | (uint32_t)boot[34] << 16 | (uint32_t)boot[35] << 24;
    struct block_device device = { .sector_count = (uint64_t)size / 512, .sector_size = 512,
        .online = true, .operations = &operations };
    test_page_budget(-1);
    assert(heap_initialize() && ramfs_initialize() && fs_initialize() && ramfs_put("ram.txt", "island", 6));
    if (!strcmp(argv[2], "exercise")) fat16_write_checks(&device);
    else {
        struct heap_statistics baseline = heap_stats();
        device.read_only = !strcmp(argv[2], "readonly");
        assert(fat16_mount(&device) == FS_OK);
        fail_at = atoi(argv[3]); tear = atoi(argv[4]); size_t transferred = 0;
        read_failure = !strcmp(argv[2], "readfail");
        if (!strcmp(argv[2], "oom")) test_page_budget(0);
        enum fs_error error;
        if (!strcmp(argv[2], "readonly")) {
            fs_reference file;
            assert(!fat16_info().writable && fs_open(NULL, "/disk/TARGET.BIN", FS_WRITE, &file) == FS_READ_ONLY);
            error = fs_replace(NULL, "/disk/NEW.BIN", "x", 1);
            assert(error == FS_READ_ONLY && !writes && !events);
        } else {
            error = fat16_write_operation(argv[2], &transferred);
            if ((fail_at >= 0 && events > (unsigned)fail_at) || read_failure) {
                assert(error == (tear == 1024 ? FS_TIMEOUT : tear == 2048 ? FS_DEVICE_FAULT : FS_IO_ERROR) &&
                    fat16_info().faulted && !fat16_info().writable);
                unsigned stopped = events;
                assert(fs_replace(NULL, "/disk/AGAIN.BIN", "x", 1) == FS_IO_ERROR && events == stopped);
                fs_reference ram; char data[6];
                assert(fs_open(NULL, "/ram.txt", FS_READ, &ram) == FS_OK &&
                    fs_read(ram, 0, data, 6).transferred == 6 && !memcmp(data, "island", 6) && fs_close(ram) == FS_OK);
            } else if (!strcmp(argv[2], "reject") || !strncmp(argv[2], "full", 4)) {
                assert(error == (!strncmp(argv[2], "full", 4) ? FS_NO_SPACE : FS_IO_ERROR) && !writes && !events && !fat16_info().faulted);
            } else if (!strcmp(argv[2], "oom")) {
                assert(error == FS_NO_MEMORY && !writes && !events && !fat16_info().faulted);
                test_page_budget(-1);
            } else if (!strcmp(argv[2], "attribute") || !strcmp(argv[2], "hidden") ||
                       !strcmp(argv[2], "lfn") || !strcmp(argv[2], "orphan")) {
                enum fs_error expected = !strcmp(argv[2], "attribute") ? FS_ACCESS :
                    !strcmp(argv[2], "hidden") ? FS_NOT_EMPTY : FS_UNSUPPORTED;
                assert(error == expected && !writes && !events && !fat16_info().faulted);
            } else assert(error == FS_OK);
        }
        assert(fat16_unmount() == FS_OK && heap_stats().used_bytes == baseline.used_bytes &&
            heap_stats().allocations == baseline.allocations && !fs_stats().objects && !fs_stats().references);
        printf("result=%s events=%u writes=%u transferred=%zu\n", fs_error_name(error), events, writes, transferred);
    }
    assert(!fclose(image));
    if (trace) assert(!fclose(trace));
    puts("PASS: FAT16 write ownership and bounds");
}
