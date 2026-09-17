#include <stdint.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/keyboard.h>
#include <rum/pic.h>
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

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

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

    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    print("\n  rum\n");
    terminal_set_color(VGA_WHITE, VGA_BLACK);
    print("  An island of our own.\n\n");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("  rum OS v0.1.0 | 32-bit x86\n");
    print("  Hello, kernel world!\n\n");
    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print("  [ok] Multiboot handoff\n");
    print("  [ok] C kernel and stack\n");
    print("  [ok] VGA text console\n");
    print("  [ok] COM1 serial logging\n");
    print("  [ok] Kernel GDT and segments\n");
    print("  [ok] IDT and CPU exception handlers\n");
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
    print("  Close QEMU to return to your host.\n\n");
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
