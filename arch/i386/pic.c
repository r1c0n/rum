#include <rum/io.h>
#include <rum/pic.h>

#define MASTER_COMMAND 0x20
#define MASTER_DATA 0x21
#define SLAVE_COMMAND 0xA0
#define SLAVE_DATA 0xA1
#define EOI 0x20

static uint8_t master_mask = 0xFF;
static uint8_t slave_mask = 0xFF;

static void command(uint16_t port, uint8_t value)
{
    outb(port, value);
    io_wait();
}

void pic_initialize(void)
{
    /* ICW1: initialize, cascade mode, edge-triggered, ICW4 follows. */
    command(MASTER_COMMAND, 0x11);
    command(SLAVE_COMMAND, 0x11);
    command(MASTER_DATA, PIC_VECTOR_BASE);
    command(SLAVE_DATA, PIC_VECTOR_BASE + 8);
    command(MASTER_DATA, 1 << 2); /* Slave connected to master IRQ2. */
    command(SLAVE_DATA, 2);
    command(MASTER_DATA, 1);     /* 8086 mode; software EOIs. */
    command(SLAVE_DATA, 1);
    master_mask = slave_mask = 0xFF;
    outb(MASTER_DATA, master_mask);
    outb(SLAVE_DATA, slave_mask);
}

void pic_mask(uint8_t irq)
{
    if (irq >= 16)
        return;
    if (irq < 8) {
        master_mask |= (uint8_t)(1u << irq);
    } else {
        slave_mask |= (uint8_t)(1u << (irq - 8));
        if (slave_mask == 0xFF)
            master_mask |= 1 << 2;
    }
    outb(SLAVE_DATA, slave_mask);
    outb(MASTER_DATA, master_mask);
}

void pic_unmask(uint8_t irq)
{
    if (irq >= 16)
        return;
    if (irq < 8) {
        master_mask &= (uint8_t)~(1u << irq);
    } else {
        slave_mask &= (uint8_t)~(1u << (irq - 8));
        master_mask &= (uint8_t)~(1u << 2);
    }
    outb(SLAVE_DATA, slave_mask);
    outb(MASTER_DATA, master_mask);
}

bool pic_irq_is_real(uint8_t irq)
{
    if (irq == 7) {
        outb(MASTER_COMMAND, 0x0B); /* Read in-service register. */
        return (inb(MASTER_COMMAND) & 0x80) != 0;
    }
    if (irq == 15) {
        outb(SLAVE_COMMAND, 0x0B);
        if ((inb(SLAVE_COMMAND) & 0x80) == 0) {
            /* Slave did not accept an IRQ; master accepted the cascade. */
            outb(MASTER_COMMAND, EOI);
            return false;
        }
    }
    return irq < 16;
}

void pic_end_of_interrupt(uint8_t irq)
{
    if (irq >= 16)
        return;
    if (irq >= 8)
        outb(SLAVE_COMMAND, EOI);
    outb(MASTER_COMMAND, EOI);
}
