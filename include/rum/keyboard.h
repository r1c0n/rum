#ifndef RUM_KEYBOARD_H
#define RUM_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

struct task_event;

/* Initialize with CPU interrupts disabled; IRQ1 is unmasked by the caller. */
bool keyboard_initialize(void);
bool keyboard_read(char *character);
uint32_t keyboard_dropped(void);
/* Signaled only when IRQ1 queues readable input. Safe for task_wait(). */
struct task_event *keyboard_input_event(void);

#endif
