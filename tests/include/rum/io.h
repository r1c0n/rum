#ifndef RUM_TEST_IO_H
#define RUM_TEST_IO_H
#include <stdint.h>
/* Console tests map VGA RAM in a host process and omit privileged port I/O. */
static inline void outb(uint16_t port, uint8_t value) { (void)port; (void)value; }
static inline uint8_t inb(uint16_t port) { (void)port; return 0; }
#endif
