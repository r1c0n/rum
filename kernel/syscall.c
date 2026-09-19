#include <rum/abi/error.h>
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/syscall.h>
#include <rum/task.h>

void syscall_dispatch(struct exception_frame *frame)
{
    if (!frame || frame->vector != RUM_SYSCALL_VECTOR || frame->error ||
        !exception_frame_from_user(frame) || !task_current_is_process()) {
        if (frame) exception_dispatch(frame);
        cpu_halt();
    }

    /* The interrupt gate protects frame construction. Syscalls run with IRQs
       enabled so device delivery and blocking task waits remain possible. */
    cpu_interrupt_enable();
    switch (frame->eax) {
    case RUM_SYS_EXIT:
        task_exit_with_status((rum_result_t)frame->ebx);
    case RUM_SYS_GETPID:
        frame->eax = task_current_process_id();
        return;
    case RUM_SYS_READ:
    case RUM_SYS_WRITE:
        frame->eax = (uint32_t)-RUM_ENOSYS;
        return;
    default:
        frame->eax = (uint32_t)-RUM_ENOSYS;
        return;
    }
}
