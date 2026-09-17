#ifndef RUM_TERMINAL_H
#define RUM_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};

void terminal_initialize(void);
void terminal_set_color(enum vga_color foreground, enum vga_color background);
void terminal_putchar(char c);
void terminal_write(const char *text, size_t length);
void terminal_writestring(const char *text);

#endif
