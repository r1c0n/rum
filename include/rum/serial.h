#ifndef RUM_SERIAL_H
#define RUM_SERIAL_H

void serial_initialize(void);
void serial_putchar(char c);
void serial_writestring(const char *text);

#endif
