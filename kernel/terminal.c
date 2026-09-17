#include <rum/io.h>
#include <rum/terminal.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define TEXT_HEIGHT (VGA_HEIGHT - 1)

static volatile uint16_t *const buffer = (volatile uint16_t *)0xB8000;
static size_t row;
static size_t column;
static uint8_t color;

static uint16_t entry(char character)
{
    return (uint16_t)(unsigned char)character | (uint16_t)color << 8;
}

static void update_cursor(void)
{
    uint16_t position = (uint16_t)(row * VGA_WIDTH + column);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)position);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(position >> 8));
}

static void clear_row(size_t target)
{
    for (size_t x = 0; x < VGA_WIDTH; ++x)
        buffer[target * VGA_WIDTH + x] = entry(' ');
}

static void advance_row(void)
{
    column = 0;
    if (++row < TEXT_HEIGHT)
        return;
    for (size_t y = 1; y < TEXT_HEIGHT; ++y)
        for (size_t x = 0; x < VGA_WIDTH; ++x)
            buffer[(y - 1) * VGA_WIDTH + x] = buffer[y * VGA_WIDTH + x];
    row = TEXT_HEIGHT - 1;
    clear_row(row);
}

void terminal_set_color(enum vga_color foreground, enum vga_color background)
{
    color = (uint8_t)((unsigned)foreground | (unsigned)background << 4);
}

void terminal_initialize(void)
{
    row = column = 0;
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (size_t y = 0; y < VGA_HEIGHT; ++y)
        clear_row(y);
    /* Enable the VGA hardware cursor with a two-scanline underline. */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 14);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 15);
    update_cursor();
}

void terminal_putchar(char c)
{
    if (c == '\n') {
        advance_row();
    } else if (c == '\r') {
        column = 0;
    } else if (c == '\b') {
        if (column > 0) {
            --column;
        } else if (row > 0) {
            --row;
            column = VGA_WIDTH - 1;
        }
        buffer[row * VGA_WIDTH + column] = entry(' ');
    } else if (c == '\t') {
        size_t spaces = 4 - column % 4;
        while (spaces-- > 0)
            terminal_putchar(' ');
    } else {
        buffer[row * VGA_WIDTH + column] = entry(c);
        if (++column == VGA_WIDTH)
            advance_row();
    }
    update_cursor();
}

void terminal_write(const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        terminal_putchar(text[i]);
}

void terminal_status(const char *text)
{
    /* Keep the bottom row outside console scrolling and leave its cursor alone. */
    for (size_t x = 0; x < VGA_WIDTH; ++x) {
        char c = ' ';
        if (*text)
            c = *text++;
        buffer[TEXT_HEIGHT * VGA_WIDTH + x] = (uint16_t)(unsigned char)c | (uint16_t)VGA_DARK_GREY << 8;
    }
}

void terminal_writestring(const char *text)
{
    while (*text)
        terminal_putchar(*text++);
}
