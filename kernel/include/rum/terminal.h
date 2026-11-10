#ifndef RUM_TERMINAL_H
#define RUM_TERMINAL_H
#include <rum/abi/terminal.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};
#define TERMINAL_WIDTH 80u
#define TERMINAL_HEIGHT 24u
struct terminal_state { uint32_t x, y, foreground, background; bool visible; };
struct terminal_snapshot { uint16_t cells[TERMINAL_WIDTH * TERMINAL_HEIGHT]; struct terminal_state state; };
struct terminal_state terminal_state(void);
bool terminal_cursor_at(uint32_t x, uint32_t y);
void terminal_capture(struct terminal_snapshot *);
void terminal_restore(const struct terminal_snapshot *);
void terminal_edit_begin(void);
void terminal_edit_render(const char *, uint32_t bytes, uint32_t cursor);
void terminal_edit_render_cells(const struct rum_terminal_cell *, uint32_t count, uint32_t cursor);
void terminal_edit_finish(void);
#define TERMINAL_SCROLLBACK_ROWS 256u
void terminal_scroll(int direction); /* -1: page up, +1: page down, 0: live. */
bool terminal_scrolled(void);
void terminal_scrollback_enable(bool enabled);

void terminal_initialize(void);
void terminal_clear(void);
void terminal_status(const char *text);
void terminal_set_color(enum vga_color foreground, enum vga_color background);
void terminal_putchar(char c);
void terminal_write(const char *text, size_t length);
void terminal_writestring(const char *text);
/* Draw inside the 80x24 console without moving its cursor or changing its color. */
bool terminal_put_at(size_t x, size_t y, char character,
                     enum vga_color foreground, enum vga_color background);
void terminal_cursor_visible(bool visible);

#endif
