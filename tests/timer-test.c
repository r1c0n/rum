/* Include the driver to seed its private clock at wraparound, without adding
   a clock-setting interface to the production kernel. Port I/O is stubbed. */
#include <assert.h>
#include <stdio.h>
#include "../hal/pc/pit.c"

static void (*handler)(void);
static unsigned requests, wakes;
static struct task_event ui_event;

void irq_register(uint8_t irq, void (*callback)(void))
{
    assert(irq == 0);
    handler = callback;
}
void task_timer_tick(void) { ++requests; }
struct task_event *task_work_event(void) { return &ui_event; }
void task_event_signal(struct task_event *event)
{
    assert(event == &ui_event);
    ++wakes;
}
int main(void)
{
    timer_initialize();
    assert(handler && !timer_ticks());
    ticks = UINT32_MAX - 2;
    for (unsigned i = 0; i < 8; ++i) handler();
    assert(timer_ticks() == 5 && requests == 8 && wakes == 8);
    puts("PASS: PIT wraparound preserves every scheduler tick and UI event");
    return 0;
}
