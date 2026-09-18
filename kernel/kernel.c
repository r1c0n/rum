#include <stdint.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/heap.h>
#include <rum/keyboard.h>
#include <rum/multiboot.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/pmm.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/terminal.h>
#include <rum/timer.h>

#if defined(__linux__) || defined(_WIN32)
#error "rum needs a bare-metal i686-elf compiler, not a Linux or Windows compiler."
#endif
#if !defined(__i386__)
#error "rum's first kernel targets 32-bit x86."
#endif

extern const char __kernel_start[], __kernel_end[];

static void serial_number(uint32_t number)
{
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    while (count) serial_putchar(digits[--count]);
}

static void print(const char *text)
{
    terminal_writestring(text);
    serial_writestring(text);
}

static void update_uptime(uint32_t seconds)
{
    char text[32] = "uptime: ";
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + seconds % 10);
        seconds /= 10;
    } while (seconds);
    unsigned length = 8;
    while (count)
        text[length++] = digits[--count];
    text[length++] = 's';
    text[length] = '\0';
    terminal_status(text);
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address);

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    timer_initialize();
    bool keyboard_ready = keyboard_initialize();
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC || multiboot_info_address == 0) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: invalid Multiboot handoff. Halting.\n");
        return;
    }
    const struct multiboot_info *info = (const void *)(uintptr_t)multiboot_info_address;
    if (!pmm_initialize(info, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end)) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: missing or invalid Multiboot memory map. Halting.\n");
        return;
    }
    if (!paging_initialize()) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: cannot allocate page tables. Halting.\n");
        return;
    }
    if (!heap_initialize() || !ramfs_initialize() || !embedded_files_install()) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: cannot initialize heap or RAM files. Halting.\n");
        return;
    }
    struct pmm_statistics memory = pmm_stats();
    serial_writestring("rum_memory_ok info="); serial_number(multiboot_info_address);
    serial_writestring(" usable="); serial_number(memory.usable_pages);
    serial_writestring(" managed="); serial_number(memory.managed_pages);
    serial_writestring(" free="); serial_number(memory.free_pages);
    serial_writestring(" limit="); serial_number(memory.limit);
    serial_writestring(" directory="); serial_number(paging_directory_address());
    serial_writestring("\n");
    struct heap_statistics heap = heap_stats();
    struct ramfs_statistics files = ramfs_stats();
    serial_writestring("rum_storage_ok mapped="); serial_number(heap.mapped_bytes);
    serial_writestring(" used="); serial_number(heap.used_bytes);
    serial_writestring(" allocations="); serial_number(heap.allocations);
    serial_writestring(" files="); serial_number(files.files);
    serial_writestring(" bytes="); serial_number(files.bytes);
    serial_writestring("\n");

    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    print("  rum\n");
    terminal_set_color(VGA_WHITE, VGA_BLACK);
    print("  An island of our own.\n");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("  rum OS v0.1.0 | 32-bit x86\n");
    print("  Hello, kernel world!\n");
    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print("  [ok] Multiboot handoff\n");
    print("  [ok] C kernel and stack\n");
    print("  [ok] VGA text console\n");
    print("  [ok] COM1 serial logging\n");
    print("  [ok] Kernel GDT and segments\n");
    print("  [ok] IDT and CPU exception handlers\n");
    print("  [ok] Multiboot memory map\n");
    print("  [ok] Physical page allocator\n");
    print("  [ok] Paging (4 KiB pages, null guard)\n");
    print("  [ok] Kernel heap (16-byte alignment)\n");
    print("  [ok] RAM filesystem and embedded files\n");
    print("  [ok] PIC remapped (IRQ0/IRQ1 only)\n");
    print("  [ok] PIT timer at 100 Hz\n");
    if (keyboard_ready) {
        print("  [ok] PS/2 keyboard (US layout)\n\n");
    } else {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("  [failed] PS/2 keyboard initialization\n\n");
    }
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("  Type 'help' for commands; Backspace edits.\n");
    print("  Close QEMU to return to your host.\n");
    shell_initialize();
    update_uptime(0);
    pic_unmask(0);
    if (keyboard_ready)
        pic_unmask(1);
    serial_writestring("rum_boot_ok\n");
    cpu_interrupt_enable();
    uint32_t displayed = 0;
    for (;;) {
        uint32_t flags = cpu_interrupt_save();
        uint32_t seconds = timer_ticks() / TIMER_HZ;
        char character;
        bool available = keyboard_read(&character);
        if (seconds != displayed || available) {
            cpu_interrupt_restore(flags);
            if (seconds != displayed) {
                displayed = seconds;
                update_uptime(seconds);
            }
            if (available)
                shell_receive(character);
        } else {
            cpu_idle();
        }
    }
}
