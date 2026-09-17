#ifndef RUM_SHELL_H
#define RUM_SHELL_H

#define SHELL_LINE_CAPACITY 256u

/* Foreground only: IRQ handlers queue input and never call the shell. */
void shell_initialize(void);
void shell_receive(char character);

#endif
