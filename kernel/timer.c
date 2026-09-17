#include <rum/interrupts.h>
#include <rum/io.h>
#include <rum/timer.h>

#define PIT_INPUT_HZ 1193182u
static volatile uint32_t ticks;

static void timer_interrupt(void)
{
    ++ticks;
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
