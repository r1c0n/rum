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
#include "fat16-checks.h"

extern const char __kernel_start[], __kernel_end[];
void kernel_main(uint32_t magic, uint32_t information);
void fat16_check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_fat_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
    }
}
void fat16_report(const char *phase, enum fs_error error)
{
    serial_writestring("rum_fat_result: "); serial_writestring(phase); serial_writestring(" ");
    serial_writestring(fs_error_name(error)); serial_writestring("\n");
}
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize();
    fat16_check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
        pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
        paging_initialize() && heap_initialize() && ramfs_initialize() && fs_initialize() &&
        ramfs_put("ram.txt", "island", 6), "FAT fixture boot");
    const struct multiboot_info *info = (const void *)(uintptr_t)information;
    char options[FS_PATH_CAPACITY + 16] = "valid";
    if (info->flags & 4) {
        const char *source = (const char *)(uintptr_t)info->cmdline;
        unsigned i = 0;
        for (; i + 1 < sizeof options && source[i]; ++i) options[i] = source[i];
        options[i] = 0;
    }
    /* QEMU's Multiboot command line may start with the kernel filename. */
    char *phase = options, *path = options;
    while (*phase) {
        path = phase;
        while (*path && *path != ' ') ++path;
        if (*path) *path++ = 0;
        if (equal(phase, "valid") || equal(phase, "mount") || equal(phase, "open")) break;
        phase = path;
    }
    fat16_check(*phase != 0, "test command line");
    (void)ata_initialize();
    fat16_checks(ata_device(), phase, path);
    terminal_writestring("rum read-only FAT16 tests passed.\n");
    serial_writestring("rum_fat_ok\n"); cpu_halt();
}
