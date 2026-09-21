#ifndef RUM_SHELL_H
#define RUM_SHELL_H
#include <stdbool.h>
#include <stdint.h>
#include <rum/process.h>

#define SHELL_LINE_CAPACITY 256u

/* Foreground only: IRQ handlers queue input and never call the shell. */
typedef enum process_launch_error (*shell_launcher)(const char *program,
                                                    const char *arguments,
                                                    struct process_result *result);
void shell_set_launcher(shell_launcher launch);
void shell_initialize(void);
void shell_receive(char character);
bool shell_tick_due(uint32_t ticks);
void shell_tick(uint32_t ticks);

#endif
