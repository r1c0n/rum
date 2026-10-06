/* Only this isolated fixture writes test sectors. Normal rum boot just probes. */
#include <rum/ata.h>
#include <rum/cpu.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/serial.h>

#define SECTORS 1024u
#define BYTES (SECTORS * 512u)
uint8_t block_test_data[BYTES];
void kernel_main(uint32_t magic, uint32_t information);

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_block_failed: "); serial_writestring(name);
        serial_writestring("\n"); cpu_halt();
    }
}

static bool mode(const char *text, const char *name)
{
    for (unsigned i = 0; text[i] && i < 4096; ++i) {
        if (i && text[i - 1] != ' ') continue;
        unsigned j = 0;
        while (name[j] && text[i + j] == name[j]) ++j;
        if (!name[j] && (!text[i + j] || text[i + j] == ' ')) return true;
    }
    return false;
}

static void exact(struct block_result result, uint64_t count, const char *name)
{
    check(result.error == BLOCK_OK && result.completed == count, name);
}

void kernel_main(uint32_t magic, uint32_t information)
{
    serial_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "handoff");
    const struct multiboot_info *info = (const void *)(uintptr_t)information;
    const char *options = (info->flags & 4) ? (const char *)(uintptr_t)info->cmdline : "";
    struct block_result initialized = ata_initialize();
    serial_writestring("rum_block_identify: ");
    serial_writestring(block_error_name(initialized.error));
    serial_writestring("\n");
    struct block_device *disk = ata_device();
    if (mode(options, "missing")) {
        check(initialized.error == BLOCK_NO_DEVICE, "missing disk");
        check(block_read(disk, 0, 1, block_test_data, 512).error == BLOCK_NO_DEVICE, "missing read");
        check(block_write(disk, 0, 1, block_test_data, 512).error == BLOCK_NO_DEVICE, "missing write");
        check(block_flush(disk).error == BLOCK_NO_DEVICE, "missing flush");
    } else {
        check(initialized.error == BLOCK_OK && disk->sector_size == 512 && disk->sector_count == SECTORS,
              "identify");
        check(block_read(disk, SECTORS, 0, NULL, 0).error == BLOCK_OK, "zero read");
        check(block_write(disk, SECTORS, 0, NULL, 0).error == BLOCK_OK, "zero write");
        check(block_read(disk, SECTORS, 1, block_test_data, 512).error == BLOCK_RANGE, "past end");
        check(block_write(disk, SECTORS - 1, 2, block_test_data, 1024).error == BLOCK_RANGE, "cross end");
        check(block_write(disk, UINT64_MAX, 2, block_test_data, 1024).error == BLOCK_RANGE, "lba overflow");
        check(block_read(disk, 1, UINT64_MAX, block_test_data, BYTES).error == BLOCK_RANGE, "count overflow");
        check(block_write(disk, 0, 1, block_test_data, 511).error == BLOCK_INVALID, "short buffer");
        if (mode(options, "read-error") || mode(options, "write-error") || mode(options, "flush-error")) {
            memset(block_test_data, 0xab, 512);
            if (mode(options, "flush-error")) {
                /* Dirty the cache with identical bytes, otherwise QEMU may
                   elide an already-clean flush before reaching the injector. */
                exact(block_read(disk, 0, 1, block_test_data, 512), 1, "flush setup read");
                exact(block_write(disk, 0, 1, block_test_data, 512), 1, "flush setup write");
            }
            struct block_result result = mode(options, "read-error") ? block_read(disk, 0, 1, block_test_data, 512) :
                                         mode(options, "write-error") ? block_write(disk, 0, 1, block_test_data, 512) :
                                         block_flush(disk);
            check(result.error == BLOCK_IO_ERROR && !result.completed && (result.status & 1) &&
                  result.device_error, "ATA error report");
            serial_writestring("rum_block_error_reported\n");
        }
        if (mode(options, "read-error"))
            exact(block_read(disk, 1, SECTORS - 1, block_test_data + 512, BYTES - 512),
                  SECTORS - 1, "read recovery after failing sector");
        else exact(block_read(disk, 0, SECTORS, block_test_data, BYTES), SECTORS, "full read");
        if (mode(options, "write")) {
            for (unsigned sector = 0; sector < SECTORS; ++sector) {
                if (sector != 0 && sector != SECTORS - 1 && !(sector >= 13 && sector < 270)) continue;
                for (unsigned byte = 0; byte < 512; ++byte)
                    block_test_data[sector * 512 + byte] = (uint8_t)(0xa5 ^ (sector * 11 + byte * 3));
            }
            exact(block_write(disk, 0, 1, block_test_data, 512), 1, "first write");
            exact(block_write(disk, SECTORS - 1, 1, block_test_data + BYTES - 512, 512), 1, "last write");
            exact(block_write(disk, 13, 257, block_test_data + 13 * 512, 257 * 512), 257, "257-sector write");
            check(block_flush(disk).error == BLOCK_OK, "flush");
            memset(block_test_data, 0, BYTES);
            exact(block_read(disk, 0, SECTORS, block_test_data, BYTES), SECTORS, "read back");
        }
    }
    serial_writestring("rum_block_ok\n");
    cpu_halt();
}
