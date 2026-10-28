#include <rum/block.h>
#include <rum/cpu.h>

static enum block_error acquire(struct block_device *device)
{
    if (!device || !device->operations || !device->sector_size)
        return BLOCK_INVALID;
    uint32_t flags = cpu_interrupt_save();
    enum block_error error = !device->online ? BLOCK_NO_DEVICE :
                             device->busy ? BLOCK_BUSY : BLOCK_OK;
    if (error == BLOCK_OK) device->busy = true;
    cpu_interrupt_restore(flags);
    return error;
}

static void release(struct block_device *device)
{
    uint32_t flags = cpu_interrupt_save();
    device->busy = false;
    cpu_interrupt_restore(flags);
}

static enum block_error validate(const struct block_device *device, uint64_t lba,
                                 uint64_t count, const void *buffer, size_t bytes)
{
    /* Subtract instead of adding LBA/count, divide instead of multiplying bytes. */
    if (lba > device->sector_count || count > device->sector_count - lba)
        return BLOCK_RANGE;
    if (count && (!buffer || count > SIZE_MAX / device->sector_size ||
                  count > bytes / device->sector_size))
        return BLOCK_INVALID;
    return BLOCK_OK;
}

struct block_result block_read(struct block_device *device, uint64_t lba,
                               uint64_t count, void *buffer, size_t bytes)
{
    enum block_error error = acquire(device);
    if (error != BLOCK_OK) return block_result(error);
    error = validate(device, lba, count, buffer, bytes);
    struct block_result result = block_result(error);
    if (error == BLOCK_OK && count)
        result = device->operations->read ? device->operations->read(device, lba, count, buffer) :
                                           block_result(BLOCK_UNSUPPORTED);
    release(device);
    return result;
}

struct block_result block_write(struct block_device *device, uint64_t lba,
                                uint64_t count, const void *buffer, size_t bytes)
{
    enum block_error error = acquire(device);
    if (error != BLOCK_OK) return block_result(error);
    error = validate(device, lba, count, buffer, bytes);
    struct block_result result = block_result(error);
    if (error == BLOCK_OK && count) {
        if (device->read_only) result = block_result(BLOCK_READ_ONLY);
        else result = device->operations->write ? device->operations->write(device, lba, count, buffer) :
                                                 block_result(BLOCK_UNSUPPORTED);
    }
    release(device);
    return result;
}

struct block_result block_flush(struct block_device *device)
{
    enum block_error error = acquire(device);
    if (error != BLOCK_OK) return block_result(error);
    struct block_result result = device->operations->flush ? device->operations->flush(device) :
                                                            block_result(BLOCK_UNSUPPORTED);
    release(device);
    return result;
}

const char *block_error_name(enum block_error error)
{
    static const char *const names[] = {
        "ok", "invalid request", "sector range", "no device", "read only",
        "busy", "timeout", "device fault", "I/O error", "unsupported device"
    };
    return (unsigned)error < sizeof(names) / sizeof(names[0]) ? names[error] : "unknown error";
}
