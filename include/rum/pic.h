#ifndef RUM_PIC_H
#define RUM_PIC_H

#include <stdbool.h>
#include <stdint.h>

#define PIC_VECTOR_BASE 0x20

void pic_initialize(void);
/* Configure masks with CPU interrupts disabled. Slave IRQs manage IRQ2. */
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
bool pic_irq_is_real(uint8_t irq);
void pic_end_of_interrupt(uint8_t irq);

#endif
