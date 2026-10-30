#ifndef RUM_TIMER_H
#define RUM_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include <rum/abi/time.h>

#define TIMER_HZ 100u
#define TIMER_HALF_RANGE 0x80000000u

/* A future deadline must be less than half a counter cycle away. Unsigned
   subtraction is intentional and remains defined across clock wraparound. */
static inline bool timer_deadline_reached(uint32_t now, uint32_t deadline)
{
    return (uint32_t)(now - deadline) < TIMER_HALF_RANGE;
}

bool timer_duration_ticks(uint32_t milliseconds, uint32_t *result);

void timer_initialize(void);
uint32_t timer_ticks(void);

#endif
