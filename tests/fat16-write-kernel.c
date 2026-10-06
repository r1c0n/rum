#include <rum/ata.h>
#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/terminal.h>
#include "fat16-write-checks.h"
#include "../kernel/fs/fat16-internal.h"

extern const char __kernel_start[], __kernel_end[];
void kernel_main(uint32_t, uint32_t);
static struct block_device *ata;
static unsigned events, writes;
static int fail_at = -1;
static unsigned tear;
static void decimal(uint64_t);
void fat16_write_check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_fat_write_failed: "); serial_writestring(name);
        serial_writestring(" last_io="); serial_writestring(fs_error_name(fat16_volume.last_io.error));
        serial_writestring(" status="); decimal(fat16_volume.last_io.device_status);
        serial_writestring(" faulted="); decimal(fat16_info().faulted); serial_writestring("\n"); cpu_halt();
    }
}
static void decimal(uint64_t value)
{
    char text[21]; unsigned count = 0;
    do { text[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) serial_putchar(text[--count]);
}
static struct block_result diagnostic(struct block_result result)
{
    if (result.error != BLOCK_OK) {
        serial_writestring("rum_fat_write_device: "); serial_writestring(block_error_name(result.error));
        serial_writestring(" status="); decimal(result.status); serial_writestring(" error=");
        decimal(result.device_error); serial_writestring(" completed="); decimal(result.completed); serial_writestring("\n");
    }
    return result;
}
static struct block_result read_proxy(struct block_device *device, uint64_t lba, uint64_t count, void *data)
{
    (void)device; return diagnostic(block_read(ata, lba, count, data, (size_t)count * 512));
}
static struct block_result write_proxy(struct block_device *device, uint64_t lba, uint64_t count, const void *data)
{
    (void)device; ++writes;
    serial_writestring("rum_fat_write_event: W "); decimal(lba); serial_writestring("\n");
    if ((int)events++ == fail_at) {
        unsigned char partial[512];
        fat16_write_check(count == 1 && block_read(ata, lba, 1, partial, sizeof partial).error == BLOCK_OK,
                          "read before injected partial write");
        memcpy(partial, data, tear);
        if (tear) fat16_write_check(block_write(ata, lba, 1, partial, sizeof partial).error == BLOCK_OK,
                                     "real partial-sector write");
        return (struct block_result){ .error = BLOCK_IO_ERROR, .status = 0x51, .device_error = 4 };
    }
    return diagnostic(block_write(ata, lba, count, data, (size_t)count * 512));
}
static struct block_result flush_proxy(struct block_device *device)
{
    (void)device;
    serial_writestring("rum_fat_write_event: F\n");
    struct block_result result = block_flush(ata);
    if ((int)events++ == fail_at && result.error == BLOCK_OK)
        return (struct block_result){ .error = BLOCK_IO_ERROR, .status = 0x51, .device_error = 4 };
    return diagnostic(result);
}
static const struct block_operations operations = { read_proxy, write_proxy, flush_proxy };
static int integer(const char *s)
{
    bool negative = *s == '-'; if (negative) ++s;
    unsigned value = 0;
    while (*s >= '0' && *s <= '9') value = value * 10 + (unsigned)(*s++ - '0');
    return negative ? -(int)value : (int)value;
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize();
    fat16_write_check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
        pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
        paging_initialize() && heap_initialize() && ramfs_initialize() && fs_initialize() &&
        ramfs_put("ram.txt", "island", 6), "write fixture boot");
    const struct multiboot_info *info = (const void *)(uintptr_t)information;
    char options[256] = "exercise -1 0";
    if (info->flags & 4) {
        const char *source = (const char *)(uintptr_t)info->cmdline;
        unsigned i = 0;
        for (; i + 1 < sizeof options && source[i]; ++i) options[i] = source[i];
        options[i] = 0;
    }
    char *tokens[4] = {0}; unsigned count = 0;
    for (char *p = options; *p && count < 4;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        tokens[count++] = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = 0;
    }
    fat16_write_check(count >= 3, "write fixture arguments");
    char *mode = tokens[count - 3];
    fail_at = integer(tokens[count - 2]); tear = (unsigned)integer(tokens[count - 1]);
    fat16_write_check(tear <= 512 && ata_initialize().error == BLOCK_OK, "write fixture ATA and failure extent");
    ata = ata_device();
    struct block_device device = { .sector_count = ata->sector_count, .sector_size = 512, .online = true,
        .read_only = equal(mode, "readonly"), .operations = &operations };
    if (equal(mode, "exercise")) fat16_write_checks(&device);
    else if (equal(mode, "verify")) fat16_write_verify(&device);
    else {
        struct heap_statistics baseline = heap_stats();
        fat16_write_check(fat16_mount(&device) == FS_OK, "mount for guest mutation");
        enum fs_error error; size_t transferred = 0;
        if (device.read_only) {
            error = fs_replace(NULL, "/disk/NEW.BIN", "x", 1);
            fat16_write_check(error == FS_READ_ONLY && !writes && !events, "read-only guest device");
        } else {
            error = fat16_write_operation(mode, &transferred);
            if (fail_at >= 0 && events > (unsigned)fail_at) {
                fat16_write_check(error == FS_IO_ERROR && fat16_info().faulted, "faulted guest mutation");
                unsigned stopped = events;
                fat16_write_check(fs_replace(NULL, "/disk/AGAIN.BIN", "x", 1) == FS_IO_ERROR && events == stopped,
                                  "guest mount stops after uncertain writes");
            } else fat16_write_check(error == FS_OK, "successful guest mutation");
        }
        fat16_write_check(fat16_unmount() == FS_OK && heap_stats().allocations == baseline.allocations &&
            heap_stats().used_bytes == baseline.used_bytes && !fs_stats().objects && !fs_stats().references,
            "guest failure ownership cleanup");
        fs_reference ram; char data[6];
        fat16_write_check(fs_open(NULL, "/ram.txt", FS_READ, &ram) == FS_OK &&
            fs_read(ram, 0, data, sizeof data).transferred == sizeof data && !memcmp(data, "island", 6) &&
            fs_close(ram) == FS_OK, "RAM after guest disk errors");
        serial_writestring("rum_fat_write_result: "); serial_writestring(fs_error_name(error));
        serial_writestring(" "); decimal(transferred); serial_writestring("\n");
    }
    serial_writestring("rum_fat_write_ok\n"); cpu_halt();
}
