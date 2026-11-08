#include <rum/terminal_session.h>
#include <rum/terminal.h>
#include <rum/abi/error.h>
#include <rum/keyboard.h>
#include <rum/task.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>

static uint32_t owner;
static struct terminal_snapshot saved;
static bool task_context(void)
{
    if (irq_in_handler()) return false;
    uint32_t flags = cpu_interrupt_save(); cpu_interrupt_restore(flags);
    return (flags & 0x200u) != 0;
}
static void restore(void)
{
    terminal_restore(&saved); owner = 0;
    keyboard_discard_input(); task_event_signal(keyboard_input_event());
}
uint32_t terminal_session_owner(void) { return owner; }
bool terminal_session_owned(uint32_t task) { return owner && task == owner && task_console_owner_id() == owner; }
bool terminal_session_output(uint32_t task) { return !owner || task == owner; }
void terminal_session_reconcile(void)
{
    if (owner && task_console_owner_id() != owner) restore();
}
void terminal_session_exit(uint32_t task) { if (owner == task) restore(); }
rum_result_t terminal_session_begin(uint32_t task)
{
    if (!task_context()) return -RUM_EBUSY;
    terminal_session_reconcile();
    if (!task || task_console_owner_id() != task) return -RUM_EACCES;
    if (owner) return -RUM_EBUSY;
    terminal_capture(&saved); owner = task;
    terminal_clear(); terminal_cursor_visible(false);
    return 0;
}
rum_result_t terminal_session_end(uint32_t task)
{
    if (!task_context()) return -RUM_EBUSY;
    if (!terminal_session_owned(task)) return -RUM_EACCES;
    restore(); return 0;
}
