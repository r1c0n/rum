#include <rum/interrupts.h>
#include <rum/pic.h>

void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) {
        irq_dispatch(frame);
        return;
    }
    exception_dispatch(frame);
}
