#ifndef RUM_SHELL_H
#define RUM_SHELL_H
#include <stdbool.h>
#include <stdint.h>

#define SHELL_LINE_CAPACITY 256u

/* Foreground only: IRQ handlers queue input and never call the shell. */
void shell_initialize(void);
void shell_receive(char character);
bool shell_tick_due(uint32_t ticks);
void shell_tick(uint32_t ticks);

#endif
