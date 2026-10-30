#include <rum/interrupts.h>
#include <rum/io.h>
#include <rum/timer.h>
#include <rum/task.h>

#define PIT_INPUT_HZ 1193182u
static volatile uint32_t ticks;

static void timer_interrupt(void)
{
    ++ticks;
    task_timer_tick(ticks);
    /* Only UI consumers (recovery and Snake) wait on this event. Console
       reads and child/FS waits use their own events and are not tick-woken. */
    task_event_signal(task_work_event());
}

bool timer_duration_ticks(uint32_t milliseconds, uint32_t *result)
{
    if (!result || milliseconds > RUM_SLEEP_MAX_MS) return false;
    /* The maximum supported product exceeds 32 bits. An extra tick covers
       the unknown fraction of the current PIT period before the first IRQ. */
    uint64_t duration = milliseconds ? ((uint64_t)milliseconds * TIMER_HZ + 999u) / 1000u + 1u : 0;
    if (duration >= TIMER_HALF_RANGE) return false;
    *result = (uint32_t)duration;
    return true;
}

void timer_initialize(void)
{
    const uint16_t divisor = (PIT_INPUT_HZ + TIMER_HZ / 2) / TIMER_HZ;
    ticks = 0;
    irq_register(0, timer_interrupt);
    outb(0x43, 0x34); /* Channel 0, LSB/MSB, periodic mode 2, binary. */
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

uint32_t timer_ticks(void)
{
    return ticks; /* Aligned 32-bit reads are atomic on this target. */
}
