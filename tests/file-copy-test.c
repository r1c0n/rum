#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../system/lib/files.h"
static unsigned char source[1537], destination[2048];
static unsigned input_offset, output_offset, closed, opened, flushed, active;
static int alias, exists, read_failure, write_failure, flush_failure, close_failure, oversized, shrink;
void tool_print(const char *text) { (void)text; }
void tool_error(const char *name, rum_result_t error) { (void)name; (void)error; }
rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    ++opened;
    if (!strcmp(path, "source")) { assert(access == RUM_OPEN_READ && !flags); ++active; return 3; }
    assert(!strcmp(path, "destination") && access == RUM_OPEN_WRITE);
    assert(flags == (RUM_OPEN_CREATE | (exists ? RUM_OPEN_TRUNCATE : RUM_OPEN_EXCLUSIVE)));
    memset(destination, 0, sizeof destination); ++active; return 4;
}
rum_result_t rum_close(rum_handle_t fd) { assert(fd == 3 || fd == 4); ++closed; --active; return close_failure && fd == 4 ? -RUM_EIO : 0; }
rum_result_t rum_fstat(rum_handle_t fd, struct rum_file_information *info)
{
    assert(fd == 3);
    *info = (struct rum_file_information){.kind = RUM_ENTRY_FILE, .volume = 1, .generation = 2, .object_lo = 3,
        .size_lo = sizeof source - (shrink && input_offset ? 1 : 0), .size_hi = oversized}; return 0;
}
rum_result_t rum_stat(const char *path, struct rum_file_information *info)
{
    assert(!strcmp(path, "destination"));
    *info = (struct rum_file_information){.kind = RUM_ENTRY_FILE, .volume = alias ? 1 : 2, .generation = 2, .object_lo = 3};
    return alias || exists ? 0 : -RUM_ENOENT;
}
rum_result_t rum_read(rum_handle_t fd, void *data, rum_size_t count)
{
    assert(fd == 3 && count <= 512);
    if (read_failure && input_offset) return -RUM_EIO;
    if (count > 37) count = 37;
    if (count > sizeof source - input_offset) count = sizeof source - input_offset;
    memcpy(data, source + input_offset, count); input_offset += count; return (rum_result_t)count;
}
rum_result_t rum_write(rum_handle_t fd, const void *data, rum_size_t count)
{
    assert(fd == 4);
    if (write_failure && output_offset) return write_failure == 2 ? 0 : -RUM_ENOSPC;
    if (count > 13) count = 13;
    assert(output_offset + count <= sizeof destination);
    memcpy(destination + output_offset, data, count); output_offset += count; return (rum_result_t)count;
}
rum_result_t rum_flush(rum_handle_t fd) { assert(fd == 4); ++flushed; return flush_failure ? -RUM_EIO : 0; }
static void reset(void)
{
    input_offset = output_offset = closed = opened = flushed = active = 0;
    alias = exists = read_failure = write_failure = flush_failure = close_failure = oversized = shrink = 0;
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (unsigned i = 0; i < sizeof source; ++i) source[i] = (unsigned char)(i * 37 ^ i >> 3);
    struct file_copy_status status;
    assert(!file_copy("source", "destination", 0, &status) && !active && closed == 2 && flushed == 1 &&
           output_offset == sizeof source && !memcmp(source, destination, sizeof source));
    reset(); alias = 1;
    assert(file_copy("source", "destination", 1, &status) == -RUM_EINVAL && status.same_file && opened == 1 && !active);
    reset(); exists = 1;
    assert(file_copy("source", "destination", 0, &status) == -RUM_EEXIST && opened == 1 && !status.output_opened && !active);
    reset(); exists = 1; assert(!file_copy("source", "destination", 1, &status) && !active);
    reset(); oversized = 1;
    assert(file_copy("source", "destination", 0, &status) == -RUM_E2BIG && opened == 1 && !active);
    for (unsigned mode = 0; mode < 6; ++mode) {
        reset(); read_failure = mode == 0; write_failure = mode == 1 ? 1 : mode == 2 ? 2 : 0;
        flush_failure = mode == 3; close_failure = mode == 4; shrink = mode == 5;
        assert(file_copy("source", "destination", 0, &status) < 0 && status.output_opened && !active && closed == 2);
    }
    puts("PASS: cross-volume binary copy, short reads/writes, aliases, overwrite refusal, size bounds and failure cleanup"); return 0;
}
