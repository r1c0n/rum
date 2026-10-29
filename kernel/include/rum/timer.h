#ifndef RUM_TIMER_H
#define RUM_TIMER_H

#include <stdint.h>

#define TIMER_HZ 100u

void timer_initialize(void);
uint32_t timer_ticks(void);

#endif
