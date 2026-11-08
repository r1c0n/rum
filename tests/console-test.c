#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <rum/terminal.h>

static volatile uint16_t *const screen = (volatile uint16_t *)0xB8000;
static unsigned char character(size_t index) { return (unsigned char)screen[index]; }

int main(void)
{
    /* Exercise the actual console driver against a simulated VGA text page. */
    void *mapping = mmap((void *)0xB8000, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapping != MAP_FAILED);
    terminal_initialize();
    for (size_t i = 0; i < 80 * 25; ++i)
        assert(screen[i] == 0x0720);

    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writestring("A\nB");
    assert(screen[0] == 0x0B41);
    assert(character(80) == 'B');
    assert(character(1) == ' ');

    terminal_initialize();
    for (size_t i = 0; i < 80; ++i)
        terminal_putchar('x');
    terminal_putchar('y');
    assert(character(79) == 'x');
    assert(character(80) == 'y');

    terminal_initialize();
    terminal_writestring("abc\bX\rQ\tY");
    assert(character(0) == 'Q');
    assert(character(1) == ' ');
    assert(character(3) == ' ');
    assert(character(4) == 'Y');

    terminal_initialize();
    terminal_putchar('\b'); /* Backspace at the top-left must stay in bounds. */
    terminal_putchar('Z');
    assert(character(0) == 'Z');

    terminal_initialize();
    for (unsigned line = 0; line < 30; ++line) {
        terminal_putchar((char)('A' + line % 26));
        terminal_putchar('\n');
    }
    /* 24 console rows plus a reserved status row; seven lines discarded. */
    assert(character(0) == 'H');
    assert(character(22 * 80) == 'D');
    for (size_t x = 0; x < 80; ++x)
        assert(character(23 * 80 + x) == ' ');
    for (size_t x = 0; x < 80; ++x)
        assert(character(24 * 80 + x) == ' ');

    terminal_initialize();
    terminal_writestring("ab");
    terminal_status("uptime: 1s");
    terminal_putchar('c');
    assert(character(2) == 'c'); /* Status updates must not move the cursor. */
    assert(character(24 * 80) == 'u');
    for (unsigned line = 0; line < 30; ++line)
        terminal_putchar('\n');
    assert(character(24 * 80) == 'u'); /* Scrolling must preserve the status. */

    terminal_initialize();
    terminal_status("uptime: 7s");
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_putchar('A');
    assert(terminal_put_at(79, 23, 'X', VGA_YELLOW, VGA_BLACK));
    assert(screen[23 * 80 + 79] == 0x0E58);
    assert(!terminal_put_at(80, 0, 'Z', VGA_WHITE, VGA_BLACK));
    assert(!terminal_put_at(0, 24, 'Z', VGA_WHITE, VGA_BLACK));
    assert(!terminal_put_at(SIZE_MAX, SIZE_MAX, 'Z', VGA_WHITE, VGA_BLACK));
    terminal_putchar('B');
    assert(screen[0] == 0x0B41 && screen[1] == 0x0B42);
    assert(character(24 * 80) == 'u');
    terminal_cursor_visible(false);
    terminal_cursor_visible(true);
    terminal_clear();
    assert(terminal_cursor_at(77, 23));
    terminal_edit_begin();
    char long_line[514];
    for (unsigned i = 0; i < sizeof long_line; ++i) long_line[i] = (char)('a' + i % 26);
    terminal_edit_render(long_line, sizeof long_line, 2);
    assert(terminal_state().x == 79 && terminal_state().y == 16);
    for (unsigned i = 0; i < sizeof long_line; ++i) assert(character(16 * 80 + 77 + i) == long_line[i]);
    assert(character(24 * 80) == 'u');
    terminal_edit_render("short", 5, 0);
    assert(terminal_state().x == 77 && terminal_state().y == 16);
    for (unsigned i = 5; i < sizeof long_line; ++i) assert(character(16 * 80 + 77 + i) == ' ');
    terminal_edit_finish(); assert(terminal_state().x == 2 && terminal_state().y == 17);
    assert(character(24 * 80) == 'u');
    terminal_initialize(); terminal_status("uptime: scrollback test");
    terminal_scroll(-1); assert(!terminal_scrolled()); /* Empty history. */
    for (unsigned i = 0; i < 300; ++i) {
        char line[8]; snprintf(line, sizeof line, "%03u\n", i);
        terminal_set_color((enum vga_color)(i % 16), VGA_BLACK); terminal_writestring(line);
    }
    assert(character(0) == '2' && character(1) == '7' && character(2) == '7');
    struct terminal_state state = terminal_state();
    terminal_scroll(-1); assert(terminal_scrolled());
    assert(character(0) == '2' && character(1) == '5' && character(2) == '4');
    assert(screen[0] >> 8 == 254 % 16 && character(24 * 80) == 'u');
    for (unsigned i = 0; i < 20; ++i) terminal_scroll(-1);
    assert(character(0) == '0' && character(1) == '2' && character(2) == '1');
    terminal_writestring("300\n"); /* Evict the oldest row without leaving history. */
    assert(terminal_scrolled() && character(0) == '0' && character(1) == '2' && character(2) == '2');
    for (unsigned i = 0; i < 20; ++i) terminal_scroll(1);
    assert(!terminal_scrolled() && character(0) == '2' && character(1) == '7' && character(2) == '8');
    assert(terminal_state().x == state.x && terminal_state().y == state.y);
    terminal_scroll(-1); terminal_status("updated while browsing");
    assert(character(24 * 80) == 'u');
    terminal_scroll(0); assert(!terminal_scrolled());
    terminal_clear(); terminal_scroll(-1); /* Clear preserves previous text. */
    assert(terminal_scrolled() && character(0) == '2' && character(1) == '7' && character(2) == '8');
    terminal_scroll(0); assert(character(0) == ' ');
    assert(munmap(mapping, 4096) == 0);
    puts("PASS: console newlines, wrapping, colors, tabs, backspace, scrolling");
    return 0;
}
