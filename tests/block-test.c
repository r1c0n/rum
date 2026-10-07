#include <assert.h>
#include <stdio.h>
#include <rum/block.h>

static unsigned calls;
static struct block_result transfer(struct block_device *device, uint64_t lba,
                                     uint64_t count, void *buffer)
{
    (void)lba; (void)buffer;
    ++calls;
    assert(block_read(device, 0, 0, NULL, 0).error == BLOCK_BUSY);
    return (struct block_result){ .completed = count };
}
static struct block_result write_transfer(struct block_device *device, uint64_t lba,
                                          uint64_t count, const void *buffer)
{
    return transfer(device, lba, count, (void *)buffer);
}
static struct block_result flush(struct block_device *device)
{
    (void)device; ++calls;
    return (struct block_result){ .error = BLOCK_IO_ERROR, .status = 0x41, .device_error = 4 };
}

int main(void)
{
    const struct block_operations operations = { transfer, write_transfer, flush };
    struct block_device device = { .sector_count = 10, .sector_size = 512,
                                   .online = true, .operations = &operations };
    unsigned char buffer[1024];
    assert(block_read(&device, 0, 1, buffer, sizeof(buffer)).completed == 1);
    assert(block_write(&device, 9, 1, buffer, sizeof(buffer)).completed == 1);
    assert(block_read(&device, 10, 0, NULL, 0).error == BLOCK_OK);
    assert(block_write(&device, 10, 0, NULL, 0).error == BLOCK_OK);
    unsigned before = calls;
    assert(block_read(&device, 10, 1, buffer, sizeof(buffer)).error == BLOCK_RANGE);
    assert(block_write(&device, 9, 2, buffer, sizeof(buffer)).error == BLOCK_RANGE);
    assert(block_read(&device, UINT64_MAX, 2, buffer, sizeof(buffer)).error == BLOCK_RANGE);
    assert(block_write(&device, 1, UINT64_MAX, buffer, sizeof(buffer)).error == BLOCK_RANGE);
    assert(block_read(&device, 11, 0, NULL, 0).error == BLOCK_RANGE);
    assert(block_read(&device, 0, 1, NULL, 512).error == BLOCK_INVALID);
    assert(block_write(&device, 0, 2, buffer, 1023).error == BLOCK_INVALID);
    device.sector_count = UINT64_MAX;
    assert(block_read(&device, 0, SIZE_MAX / 512 + 1, buffer, SIZE_MAX).error == BLOCK_INVALID);
    device.read_only = true;
    assert(block_write(&device, 0, 1, buffer, 512).error == BLOCK_READ_ONLY);
    assert(block_write(&device, 0, 0, NULL, 0).error == BLOCK_OK);
    device.online = false;
    assert(block_read(&device, 0, 1, buffer, 512).error == BLOCK_NO_DEVICE);
    assert(block_flush(&device).error == BLOCK_NO_DEVICE);
    assert(block_read(NULL, 0, 0, NULL, 0).error == BLOCK_INVALID);
    assert(calls == before && !device.busy);
    device.online = true;
    struct block_result result = block_flush(&device);
    assert(result.error == BLOCK_IO_ERROR && result.status == 0x41 && result.device_error == 4);
    assert(!device.busy);
    puts("block API tests passed");
}
