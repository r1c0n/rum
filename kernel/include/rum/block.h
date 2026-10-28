#ifndef RUM_BLOCK_H
#define RUM_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum block_error {
    BLOCK_OK, BLOCK_INVALID, BLOCK_RANGE, BLOCK_NO_DEVICE, BLOCK_READ_ONLY,
    BLOCK_BUSY, BLOCK_TIMEOUT, BLOCK_DEVICE_FAULT, BLOCK_IO_ERROR, BLOCK_UNSUPPORTED
};

/* completed counts fully transferred sectors; a failed write may also have
   changed its in-flight sector. status/error retain the driver's diagnostics. */
struct block_result {
    enum block_error error;
    uint64_t completed;
    uint8_t status, device_error;
};

struct block_device;
struct block_operations {
    struct block_result (*read)(struct block_device *, uint64_t, uint64_t, void *);
    struct block_result (*write)(struct block_device *, uint64_t, uint64_t, const void *);
    struct block_result (*flush)(struct block_device *);
};

/* Private kernel object. Initialize before publishing; callers use the wrappers
   below, never operations directly. Requests are synchronous and non-reentrant.
   Call from task/boot context, never an interrupt handler. */
struct block_device {
    uint64_t sector_count;
    uint32_t sector_size;
    bool online, read_only, busy;
    const struct block_operations *operations;
    void *context;
};

static inline struct block_result block_result(enum block_error error)
{
    return (struct block_result){ .error = error };
}

struct block_result block_read(struct block_device *device, uint64_t lba,
                               uint64_t count, void *buffer, size_t bytes);
struct block_result block_write(struct block_device *device, uint64_t lba,
                                uint64_t count, const void *buffer, size_t bytes);
struct block_result block_flush(struct block_device *device);
const char *block_error_name(enum block_error error);

#endif
