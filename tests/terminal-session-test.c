#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <rum/terminal.h>
#include <rum/terminal_session.h>
#include <rum/task.h>
#include <rum/keyboard.h>
#include <rum/abi/error.h>
#include <rum/serial.h>

uint32_t handle_test_flags = 0x200u;
static bool in_irq;
static uint32_t input_owner = 1, discarded;
static struct task_event input;
bool irq_in_handler(void) { return in_irq; }
uint32_t task_console_owner_id(void) { return input_owner; }
void keyboard_discard_input(void) { ++discarded; }
struct task_event *keyboard_input_event(void) { return &input; }
void task_event_signal(struct task_event *event) { ++event->sequence; }
void task_notify_shell_input(void) {}
void serial_putchar(char c) { (void)c; }
void serial_writestring(const char *s) { (void)s; }
int main(void)
{
    void *mapped = mmap((void *)0xB8000, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapped != MAP_FAILED);
    terminal_initialize(); terminal_writestring("rum shell");
    struct terminal_snapshot baseline; terminal_capture(&baseline);
    in_irq = true; assert(terminal_session_begin(1) == -RUM_EBUSY); in_irq = false;
    handle_test_flags = 0; assert(terminal_session_begin(1) == -RUM_EBUSY); handle_test_flags = 0x200u;
    assert(terminal_session_begin(2) == -RUM_EACCES && !terminal_session_owner());
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        assert(!terminal_session_begin(1) && terminal_session_owned(1) && !terminal_session_owned(2));
        assert(terminal_session_output(1) && !terminal_session_output(2));
        assert(terminal_session_begin(1) == -RUM_EBUSY);
        assert(terminal_cursor_at(79, 23) && !terminal_cursor_at(80, 0) && !terminal_cursor_at(0, 24));
        terminal_set_color(VGA_YELLOW, VGA_BLUE); terminal_cursor_visible(false);
        assert(terminal_put_at(0, 0, '@', VGA_LIGHT_GREEN, VGA_BLACK));
        terminal_status("updated while in application");
        assert(terminal_session_end(2) == -RUM_EACCES);
        if (iteration % 3 == 0) assert(!terminal_session_end(1));
        else if (iteration % 3 == 1) { input_owner = 2; terminal_session_reconcile(); input_owner = 1; }
        else terminal_session_exit(1);
        assert(!terminal_session_owner() && terminal_session_output(2));
        struct terminal_snapshot after; terminal_capture(&after);
        assert(!memcmp(baseline.cells, after.cells, sizeof baseline.cells));
        assert(after.state.x == baseline.state.x && after.state.y == baseline.state.y &&
               after.state.foreground == baseline.state.foreground && after.state.background == baseline.state.background && after.state.visible);
        assert(((volatile uint16_t *)mapped)[24 * 80] == 0x0875);
        assert(discarded == iteration + 1 && input.sequence == iteration + 1);
    }
    assert(!terminal_session_begin_inline(1) && terminal_session_inline());
    terminal_session_line("> abc", 5, 2);
    assert(terminal_session_output(2) && terminal_session_redraw());
    terminal_writestring("notice\n");
    terminal_session_line("> abc", 5, 3);
    assert(!terminal_session_redraw() && terminal_state().x == 3 && terminal_state().y == 2);
    assert(!terminal_session_end(1) && discarded == 64 && terminal_state().x == 5);
    assert(((volatile uint16_t *)mapped)[80] == 0x076E); /* Background notice remains. */
    assert(!terminal_session_begin_inline(1)); terminal_session_exit(1);
    assert(discarded == 65 && !terminal_session_owner());
    assert(munmap(mapped, 4096) == 0);
    puts("PASS: terminal snapshot, colors/cursor, status guard, denied contexts, ownership changes and repeated cleanup");
    return 0;
}
