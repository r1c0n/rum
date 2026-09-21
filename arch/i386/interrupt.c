#include <rum/interrupts.h>
#include <rum/abi/syscall.h>
#include <rum/pic.h>
#include <rum/syscall.h>
#include <rum/task.h>

void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) {
        irq_dispatch(frame);
        task_cancel_on_user_return(frame);
        return;
    }
    if (frame->vector == RUM_SYSCALL_VECTOR) {
        syscall_dispatch(frame);
        task_cancel_on_user_return(frame);
        return;
    }
    exception_dispatch(frame);
}
