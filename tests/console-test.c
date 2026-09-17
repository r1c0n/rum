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

    assert(munmap(mapping, 4096) == 0);
    puts("PASS: console newlines, wrapping, colors, tabs, backspace, scrolling");
    return 0;
}
