#include <stdint.h>
#include <rum/serial.h>
#include <rum/terminal.h>

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

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address);

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address)
{
    terminal_initialize();
    serial_initialize();
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
    print("  [ok] COM1 serial logging\n\n");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("  First landfall. Next up: keyboard input and a shell.\n");
    print("  CPU idle. Close QEMU to return to your host.\n");
    serial_writestring("rum_boot_ok\n");
    /* Return to the assembly halt loop. There is no scheduler or shell yet. */
}
