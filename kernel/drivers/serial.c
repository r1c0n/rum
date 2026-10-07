#include <rum/io.h>
#include <rum/serial.h>

#define COM1 0x3F8

void serial_initialize(void)
{
    outb(COM1 + 1, 0x00); /* UART interrupts disabled. */
    outb(COM1 + 3, 0x80); /* Set divisor latch. */
    outb(COM1, 0x03);     /* 38400 baud. */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* Eight data bits, no parity, one stop bit. */
    outb(COM1 + 2, 0xC7); /* Enable and clear FIFO. */
    outb(COM1 + 4, 0x03); /* DTR and RTS. */
}

void serial_putchar(char c)
{
    /* A missing UART must not prevent the kernel from booting. */
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        if (inb(COM1 + 5) & 0x20) {
            outb(COM1, (uint8_t)c);
            return;
        }
    }
}

void serial_writestring(const char *text)
{
    while (*text) {
        if (*text == '\n')
            serial_putchar('\r');
        serial_putchar(*text++);
    }
}
