#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/pic.h>

static void (*handlers[16])(void);
static volatile uint32_t irq_counts[16];
static volatile uint32_t spurious_irqs;
static volatile uint32_t handler_depth;

bool irq_in_handler(void)
{
    return handler_depth != 0;
}

void irq_register(uint8_t irq, void (*handler)(void))
{
    if (irq >= 16)
        return;
    uint32_t flags = cpu_interrupt_save();
    handlers[irq] = handler;
    cpu_interrupt_restore(flags);
}

uint32_t irq_spurious_count(void)
{
    return spurious_irqs;
}

void irq_dispatch(const struct exception_frame *frame)
{
    if (frame->vector < PIC_VECTOR_BASE || frame->vector >= PIC_VECTOR_BASE + 16)
        return;
    uint8_t irq = (uint8_t)(frame->vector - PIC_VECTOR_BASE);
    if (!pic_irq_is_real(irq)) {
        ++spurious_irqs;
        return;
    }
    ++irq_counts[irq];
    if (handlers[irq]) {
        ++handler_depth;
        handlers[irq]();
        --handler_depth;
    } else
        pic_mask(irq); /* Keep an unconfigured source from repeatedly firing. */
    pic_end_of_interrupt(irq);
}
