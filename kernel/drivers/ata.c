#include <rum/ata.h>
#include <rum/io.h>

#define ATA_DATA 0x170
#define ATA_ERROR 0x171
#define ATA_COUNT 0x172
#define ATA_LBA_LOW 0x173
#define ATA_LBA_MID 0x174
#define ATA_LBA_HIGH 0x175
#define ATA_DEVICE 0x176
#define ATA_COMMAND 0x177
#define ATA_CONTROL 0x376
#define ATA_BSY 0x80
#define ATA_DF 0x20
#define ATA_DRQ 0x08
#define ATA_ERR 0x01
#define ATA_POLL_LIMIT 1000000u
#define ATA_FLUSH_POLL_LIMIT 50000000u
#define ATA_SECTOR_SIZE 512u
#define ATA_LBA_LIMIT (UINT64_C(1) << 28)

static struct block_device disk;
static bool needs_flush;

static void settle(void)
{
    /* Four alternate-status reads provide the ATA select/command delay. */
    for (unsigned i = 0; i < 4; ++i) (void)inb(ATA_CONTROL);
}

enum poll_phase { POLL_IDLE, POLL_DATA, POLL_COMPLETE };

static struct block_result poll_limit(enum poll_phase phase, unsigned limit)
{
    uint8_t status = 0;
    for (unsigned i = 0; i < limit; ++i) {
        status = inb(ATA_CONTROL);
        if (status == 0 || status == 0xff) {
            disk.online = false;
            return (struct block_result){ .error = BLOCK_NO_DEVICE, .status = status };
        }
        if (status & ATA_BSY) continue; /* Other bits are invalid while busy. */
        if (status & ATA_DF) {
            disk.online = false;
            return (struct block_result){ .error = BLOCK_DEVICE_FAULT, .status = status,
                                          .device_error = inb(ATA_ERROR) };
        }
        /* ERR from an earlier failed command is cleared by the next command. */
        if (phase != POLL_IDLE && (status & ATA_ERR))
            return (struct block_result){ .error = BLOCK_IO_ERROR, .status = status,
                                          .device_error = inb(ATA_ERROR) };
        if ((phase == POLL_DATA) == ((status & ATA_DRQ) != 0))
            return (struct block_result){ .status = status };
    }
    /* Do not issue another command while a timed-out command may still run. */
    disk.online = false;
    return (struct block_result){ .error = BLOCK_TIMEOUT, .status = status };
}
static struct block_result poll(enum poll_phase phase) { return poll_limit(phase, ATA_POLL_LIMIT); }

static struct block_result command(uint32_t lba, uint8_t opcode)
{
    struct block_result result = poll(POLL_IDLE);
    if (result.error != BLOCK_OK) return result;
    outb(ATA_DEVICE, (uint8_t)(0xe0 | (lba >> 24)));
    settle();
    result = poll(POLL_IDLE);
    if (result.error != BLOCK_OK) return result;
    outb(ATA_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, opcode);
    settle();
    return poll(POLL_DATA);
}

static struct block_result transfer(struct block_device *device, uint64_t lba,
                                     uint64_t count, void *buffer, bool write)
{
    (void)device;
    uint8_t *bytes = buffer;
    struct block_result result = block_result(BLOCK_OK);
    for (uint64_t done = 0; done < count; ++done) {
        result = command((uint32_t)(lba + done), write ? 0x30 : 0x20);
        if (result.error == BLOCK_OK) {
            for (unsigned i = 0; i < ATA_SECTOR_SIZE; i += 2) {
                if (write) outw(ATA_DATA, (uint16_t)(bytes[i] | ((uint16_t)bytes[i + 1] << 8)));
                else {
                    uint16_t word = inw(ATA_DATA);
                    bytes[i] = (uint8_t)word; bytes[i + 1] = (uint8_t)(word >> 8);
                }
            }
            settle();
            result = poll(POLL_COMPLETE);
        }
        result.completed = done;
        if (result.error != BLOCK_OK) return result;
        result.completed = done + 1;
        bytes += ATA_SECTOR_SIZE;
    }
    return result;
}

static struct block_result read_sectors(struct block_device *device, uint64_t lba,
                                         uint64_t count, void *buffer)
{
    return transfer(device, lba, count, buffer, false);
}

static struct block_result write_sectors(struct block_device *device, uint64_t lba,
                                          uint64_t count, const void *buffer)
{
    return transfer(device, lba, count, (void *)buffer, true);
}

static struct block_result flush_cache(struct block_device *device)
{
    (void)device;
    if (!needs_flush) return block_result(BLOCK_OK);
    struct block_result result = poll(POLL_IDLE);
    if (result.error != BLOCK_OK) return result;
    outb(ATA_DEVICE, 0xe0);
    settle();
    result = poll(POLL_IDLE);
    if (result.error != BLOCK_OK) return result;
    outb(ATA_COMMAND, 0xe7);
    settle();
    /* Cache flushes can include slow host fsync/device media work. Preserve a
       finite boot-safe bound while allowing more time than a sector transfer. */
    return poll_limit(POLL_COMPLETE, ATA_FLUSH_POLL_LIMIT);
}

static const struct block_operations operations = { read_sectors, write_sectors, flush_cache };

struct block_result ata_initialize(void)
{
    disk = (struct block_device){ .sector_size = ATA_SECTOR_SIZE, .operations = &operations };
    needs_flush = false;
    outb(ATA_CONTROL, 2); /* nIEN: polling driver never enables IDE interrupts. */
    outb(ATA_DEVICE, 0xa0);
    settle();
    struct block_result result = poll(POLL_IDLE);
    if (result.error != BLOCK_OK) return result;
    outb(ATA_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, 0xec); /* IDENTIFY DEVICE */
    settle();
    result = poll(POLL_DATA);
    if (result.error != BLOCK_OK) {
        if (result.error == BLOCK_IO_ERROR && (inb(ATA_LBA_MID) || inb(ATA_LBA_HIGH)))
            result.error = BLOCK_UNSUPPORTED; /* ATAPI signature */
        return result;
    }
    uint16_t identify[256];
    for (unsigned i = 0; i < 256; ++i) identify[i] = inw(ATA_DATA);
    settle();
    result = poll(POLL_COMPLETE);
    if (result.error != BLOCK_OK) return result;
    uint64_t sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    bool geometry_valid = (identify[106] & 0xc000) == 0x4000;
    bool long_sector = geometry_valid && (identify[106] & (1u << 12));
    uint32_t words = (uint32_t)identify[117] | ((uint32_t)identify[118] << 16);
    bool commands_valid = (identify[83] & 0xc000) == 0x4000;
    bool flush_supported = commands_valid && (identify[83] & (1u << 12));
    bool write_cache = (identify[85] & (1u << 5)) != 0;
    if ((identify[0] & 0x8000) || !(identify[49] & (1u << 9)) || !sectors ||
        sectors > ATA_LBA_LIMIT || (long_sector && words != 256) ||
        (write_cache && !flush_supported))
        return block_result(BLOCK_UNSUPPORTED);
    disk.sector_count = sectors;
    needs_flush = flush_supported;
    disk.online = true;
    return result;
}

struct block_device *ata_device(void)
{
    return &disk;
}
