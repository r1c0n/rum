#include <rum/terminal_session.h>
#include <rum/terminal.h>
#include <rum/abi/error.h>
#include <rum/keyboard.h>
#include <rum/task.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/serial.h>

static uint32_t owner;
static struct terminal_snapshot saved;
static bool inline_mode, dirty;
static char line[RUM_TERM_MAX_LINE];
static uint32_t line_bytes, line_cursor;
static bool task_context(void)
{
    if (irq_in_handler()) return false;
    uint32_t flags = cpu_interrupt_save(); cpu_interrupt_restore(flags);
    return (flags & 0x200u) != 0;
}
static void restore(bool forced)
{
    terminal_scroll(0);
    if (inline_mode) {
        if (!dirty) terminal_edit_finish();
        terminal_set_color((enum vga_color)saved.state.foreground, (enum vga_color)saved.state.background);
        terminal_cursor_visible(saved.state.visible);
    } else { terminal_restore(&saved); terminal_scrollback_enable(true); }
    if (!inline_mode || forced) keyboard_discard_input();
    owner = 0; inline_mode = dirty = false; line_bytes = line_cursor = 0;
    task_event_signal(keyboard_input_event());
}
uint32_t terminal_session_owner(void) { return owner; }
bool terminal_session_owned(uint32_t task) { return owner && task == owner && task_console_owner_id() == owner; }
bool terminal_session_inline(void) { return owner && inline_mode; }
bool terminal_session_redraw(void) { return owner && inline_mode && dirty; }
bool terminal_session_output(uint32_t task)
{
    if (!owner || task == owner) return true;
    if (!inline_mode) return false;
    if (!dirty) { terminal_edit_finish(); terminal_putchar('\n'); serial_writestring("\n"); }
    dirty = true; task_event_signal(keyboard_input_event()); task_notify_shell_input();
    return true;
}
void terminal_session_reconcile(void)
{
    if (owner && task_console_owner_id() != owner) restore(true);
}
void terminal_session_exit(uint32_t task) { if (owner == task) restore(true); }
static rum_result_t begin(uint32_t task, bool inline_session)
{
    if (!task_context()) return -RUM_EBUSY;
    terminal_session_reconcile();
    if (!task || task_console_owner_id() != task) return -RUM_EACCES;
    if (owner) return -RUM_EBUSY;
    terminal_scroll(0);
    if (inline_session) saved.state = terminal_state(); else terminal_capture(&saved);
    owner = task; inline_mode = inline_session; dirty = false; line_bytes = line_cursor = 0;
    if (inline_mode) { terminal_edit_begin(); terminal_cursor_visible(true); }
    else { terminal_scrollback_enable(false); terminal_clear(); terminal_cursor_visible(false); }
    return 0;
}
rum_result_t terminal_session_begin(uint32_t task) { return begin(task, false); }
rum_result_t terminal_session_begin_inline(uint32_t task) { return begin(task, true); }
rum_result_t terminal_session_end(uint32_t task)
{
    if (!task_context()) return -RUM_EBUSY;
    if (!terminal_session_owned(task)) return -RUM_EACCES;
    restore(false); return 0;
}
static void session_line(const char *text, const struct rum_terminal_cell *cells, uint32_t bytes, uint32_t cursor)
{
    if (dirty) { terminal_edit_begin(); line_bytes = line_cursor = 0; dirty = false; }
    bool prefix = bytes >= line_bytes;
    for (uint32_t i = 0; prefix && i < line_bytes; ++i) prefix = text[i] == line[i];
    if (prefix && line_cursor == line_bytes && cursor == bytes) {
        for (uint32_t i = line_bytes; i < bytes; ++i) serial_putchar(text[i]);
    } else if (line_bytes == bytes + 1 && cursor == bytes && line_cursor == line_bytes) {
        bool same = true; for (uint32_t i = 0; i < bytes; ++i) same &= text[i] == line[i];
        if (same) serial_writestring("\b \b"); else prefix = false;
        if (!same) goto redraw;
    } else {
redraw:
        serial_writestring("\r\x1b[2K");
        for (uint32_t i = 0; i < bytes; ++i) serial_putchar(text[i]);
        if (cursor < bytes) {
            char digits[10]; unsigned count = 0; uint32_t distance = bytes - cursor;
            do { digits[count++] = (char)('0' + distance % 10); distance /= 10; } while (distance);
            serial_writestring("\x1b["); while (count) serial_putchar(digits[--count]); serial_putchar('D');
        }
    }
    if (cells) terminal_edit_render_cells(cells, bytes, cursor);
    else terminal_edit_render(text, bytes, cursor);
    for (uint32_t i = 0; i < bytes; ++i) line[i] = text[i];
    line_bytes = bytes; line_cursor = cursor;
}
void terminal_session_line(const char *text, uint32_t bytes, uint32_t cursor) { session_line(text, NULL, bytes, cursor); }
void terminal_session_styled_line(const struct rum_terminal_cell *cells, uint32_t count, uint32_t cursor)
{
    char text[RUM_TERM_MAX_LINE];
    for (uint32_t i = 0; i < count; ++i) text[i] = (char)cells[i].character;
    session_line(text, cells, count, cursor);
}
