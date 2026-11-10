#include <rum/io.h>
#include <rum/terminal.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define TEXT_HEIGHT (VGA_HEIGHT - 1)

static volatile uint16_t *const buffer = (volatile uint16_t *)0xB8000;
static size_t row;
static size_t column;
static uint8_t color;
static bool cursor_visible;
static size_t edit_row, edit_column, edit_length;
/* Live output and the viewed page are independent. All storage is bounded;
   application canvases temporarily suspend history collection. */
static uint16_t live[VGA_WIDTH * TEXT_HEIGHT];
static uint16_t history[TERMINAL_SCROLLBACK_ROWS][VGA_WIDTH];
static size_t history_first, history_count, view_offset;
static bool record_history;

static void cursor_shape(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, cursor_visible && !view_offset ? 14 : 14 | 0x20);
}
static void cell(size_t index, uint16_t value)
{
    live[index] = value;
    if (!view_offset) buffer[index] = value;
}
static void paint(void)
{
    size_t start = history_count - view_offset;
    for (size_t y = 0; y < TEXT_HEIGHT; ++y) {
        size_t at = start + y;
        const uint16_t *source = at < history_count ? history[(history_first + at) % TERMINAL_SCROLLBACK_ROWS] : live + (at - history_count) * VGA_WIDTH;
        for (size_t x = 0; x < VGA_WIDTH; ++x) buffer[y * VGA_WIDTH + x] = source[x];
    }
    cursor_shape();
}
static void remember(const uint16_t *source)
{
    if (!record_history) return;
    size_t at = (history_first + history_count) % TERMINAL_SCROLLBACK_ROWS;
    if (history_count == TERMINAL_SCROLLBACK_ROWS) history_first = (history_first + 1) % TERMINAL_SCROLLBACK_ROWS;
    else ++history_count;
    for (size_t x = 0; x < VGA_WIDTH; ++x) history[at][x] = source[x];
    if (view_offset && view_offset < history_count) ++view_offset;
}

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
        cell(target * VGA_WIDTH + x, entry(' '));
}

static void scroll_rows(size_t rows)
{
    for (size_t y = 0; y < rows; ++y) remember(live + y * VGA_WIDTH);
    for (size_t y = 0; y < TEXT_HEIGHT - rows; ++y)
        for (size_t x = 0; x < VGA_WIDTH; ++x) cell(y * VGA_WIDTH + x, live[(y + rows) * VGA_WIDTH + x]);
    for (size_t y = TEXT_HEIGHT - rows; y < TEXT_HEIGHT; ++y) clear_row(y);
    if (view_offset) paint();
}

static void advance_row(void)
{
    column = 0;
    if (++row < TEXT_HEIGHT)
        return;
    scroll_rows(1);
    row = TEXT_HEIGHT - 1;
}

void terminal_set_color(enum vga_color foreground, enum vga_color background)
{
    color = (uint8_t)((unsigned)foreground | (unsigned)background << 4);
}

void terminal_initialize(void)
{
    row = column = 0;
    history_first = history_count = view_offset = 0; record_history = true;
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (size_t y = 0; y < TEXT_HEIGHT; ++y)
        clear_row(y);
    for (size_t x = 0; x < VGA_WIDTH; ++x) buffer[TEXT_HEIGHT * VGA_WIDTH + x] = entry(' ');
    /* Enable the VGA hardware cursor with a two-scanline underline. */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 14);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 15);
    cursor_visible = true;
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
        cell(row * VGA_WIDTH + column, entry(' '));
    } else if (c == '\t') {
        size_t spaces = 4 - column % 4;
        while (spaces-- > 0)
            terminal_putchar(' ');
    } else {
        cell(row * VGA_WIDTH + column, entry(c));
        if (++column == VGA_WIDTH)
            advance_row();
    }
    update_cursor();
}

void terminal_clear(void)
{
    /* Clear console text only; keep the timer status and current color. */
    terminal_scroll(0);
    size_t used = TEXT_HEIGHT;
    while (used) {
        bool blank = true;
        for (size_t x = 0; x < VGA_WIDTH; ++x) blank &= (live[(used - 1) * VGA_WIDTH + x] & 255) == ' ';
        if (!blank) break;
        --used;
    }
    for (size_t y = 0; y < used; ++y) remember(live + y * VGA_WIDTH);
    row = column = 0;
    for (size_t y = 0; y < TEXT_HEIGHT; ++y)
        clear_row(y);
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

bool terminal_put_at(size_t x, size_t y, char character,
                     enum vga_color foreground, enum vga_color background)
{
    if (x >= VGA_WIDTH || y >= TEXT_HEIGHT) return false;
    uint8_t attribute = (uint8_t)((unsigned)foreground | (unsigned)background << 4);
    cell(y * VGA_WIDTH + x, (uint16_t)(unsigned char)character | (uint16_t)attribute << 8);
    return true;
}

void terminal_cursor_visible(bool visible)
{
    cursor_visible = visible;
    cursor_shape();
}

struct terminal_state terminal_state(void)
{
    return (struct terminal_state){ .x = column, .y = row, .foreground = color & 15,
        .background = color >> 4, .visible = cursor_visible };
}
bool terminal_cursor_at(uint32_t x, uint32_t y)
{
    if (x >= TERMINAL_WIDTH || y >= TERMINAL_HEIGHT) return false;
    column = x; row = y; update_cursor(); return true;
}
void terminal_capture(struct terminal_snapshot *snapshot)
{
    for (unsigned i = 0; i < TERMINAL_WIDTH * TERMINAL_HEIGHT; ++i) snapshot->cells[i] = live[i];
    snapshot->state = terminal_state();
}
void terminal_restore(const struct terminal_snapshot *snapshot)
{
    terminal_scroll(0);
    for (unsigned i = 0; i < TERMINAL_WIDTH * TERMINAL_HEIGHT; ++i) cell(i, snapshot->cells[i]);
    terminal_set_color((enum vga_color)snapshot->state.foreground, (enum vga_color)snapshot->state.background);
    (void)terminal_cursor_at(snapshot->state.x, snapshot->state.y);
    terminal_cursor_visible(snapshot->state.visible);
}
void terminal_edit_begin(void) { edit_row = row; edit_column = column; edit_length = 0; }
static void edit_render(const char *text, const struct rum_terminal_cell *cells, uint32_t bytes, uint32_t cursor)
{
    size_t extent = bytes > edit_length ? bytes : edit_length;
    size_t last = edit_row + (edit_column + extent) / VGA_WIDTH;
    if (last >= TEXT_HEIGHT) {
        size_t shift = last - TEXT_HEIGHT + 1;
        scroll_rows(shift);
        edit_row -= shift;
    }
    for (size_t i = 0; i < extent; ++i) {
        uint16_t value = entry(i < bytes && text ? text[i] : ' ');
        if (i < bytes && cells) value = (uint16_t)cells[i].character |
            (uint16_t)(cells[i].foreground | cells[i].background << 4) << 8;
        cell(edit_row * VGA_WIDTH + edit_column + i, value);
    }
    edit_length = bytes;
    row = edit_row + (edit_column + cursor) / VGA_WIDTH; column = (edit_column + cursor) % VGA_WIDTH;
    update_cursor();
}
void terminal_edit_render(const char *text, uint32_t bytes, uint32_t cursor) { edit_render(text, NULL, bytes, cursor); }
void terminal_edit_render_cells(const struct rum_terminal_cell *cells, uint32_t count, uint32_t cursor)
{
    edit_render(NULL, cells, count, cursor);
}
void terminal_edit_finish(void)
{
    row = edit_row + (edit_column + edit_length) / VGA_WIDTH; column = (edit_column + edit_length) % VGA_WIDTH;
    update_cursor();
}
void terminal_scroll(int direction)
{
    size_t previous = view_offset;
    if (!record_history && direction) return;
    if (!direction) view_offset = 0;
    else if (direction < 0) {
        size_t next = view_offset + TEXT_HEIGHT - 1;
        view_offset = next < history_count ? next : history_count;
    } else view_offset = view_offset > TEXT_HEIGHT - 1 ? view_offset - (TEXT_HEIGHT - 1) : 0;
    if (view_offset != previous) { paint(); update_cursor(); }
}
bool terminal_scrolled(void) { return view_offset != 0; }
void terminal_scrollback_enable(bool enabled) { terminal_scroll(0); record_history = enabled; }
