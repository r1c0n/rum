#ifndef RUM_SNAKE_H
#define RUM_SNAKE_H
#include <stdbool.h>
#include <stdint.h>
#include <rum/timer.h>

#define SNAKE_STEP_TICKS (TIMER_HZ * 3u / 20u) /* 150 ms, one cell per update. */

/* Foreground-only application. IRQs keep queueing keys and updating ticks. */
bool snake_start(uint32_t now);
bool snake_active(void);
void snake_receive(char character, uint32_t now);
bool snake_tick_due(uint32_t now);
void snake_tick(uint32_t now);
#endif
