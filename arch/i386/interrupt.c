#include <rum/interrupts.h>
#include <rum/abi/syscall.h>
#include <rum/pic.h>
#include <rum/syscall.h>

void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) {
        irq_dispatch(frame);
        return;
    }
    if (frame->vector == RUM_SYSCALL_VECTOR) {
        syscall_dispatch(frame);
        return;
    }
    exception_dispatch(frame);
}
