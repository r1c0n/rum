#include <rum/abi/error.h>
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/keyboard.h>
#include <rum/memory_layout.h>
#include <rum/paging.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/terminal.h>

#define CONSOLE_CHUNK 128u

static rum_result_t error(uint32_t number)
{
    return -(rum_result_t)number;
}

static bool valid_user_range(uint32_t address, uint32_t bytes)
{
    return bytes == 0 || memory_range_contains(RUM_USER_BASE, RUM_USER_END, address, bytes);
}

static rum_result_t console_write(struct paging_space *space, rum_handle_t handle,
                                  uint32_t address, uint32_t bytes)
{
    if (handle != RUM_STDOUT && handle != RUM_STDERR) return error(RUM_EBADF);
    if (!bytes) return 0;
    if (!valid_user_range(address, bytes)) return error(RUM_EFAULT);
    uint32_t chunk = bytes < CONSOLE_CHUNK ? bytes : CONSOLE_CHUNK;
    if (!paging_user_accessible(space, address, chunk, false)) return error(RUM_EFAULT);
    char buffer[CONSOLE_CHUNK];
    if (!paging_copy_from_user(space, buffer, address, chunk)) return error(RUM_EFAULT);
    terminal_write(buffer, chunk);
    for (uint32_t i = 0; i < chunk; ++i) serial_putchar(buffer[i]);
    return (rum_result_t)chunk;
}

static rum_result_t console_read(struct paging_space *space, rum_handle_t handle,
                                 uint32_t address, uint32_t capacity)
{
    if (handle != RUM_STDIN) return error(RUM_EBADF);
    if (!capacity) return 0;
    if (!valid_user_range(address, capacity)) return error(RUM_EFAULT);
    uint32_t chunk = capacity < CONSOLE_CHUNK ? capacity : CONSOLE_CHUNK;
    if (!paging_user_accessible(space, address, chunk, true)) return error(RUM_EFAULT);

    char buffer[CONSOLE_CHUNK];
    for (;;) {
        task_cancel_current_if_requested();
        struct task_event *event = keyboard_input_event();
        uint32_t observed = task_event_sequence(event);
        uint32_t count = 0;
        while (count < chunk && keyboard_read(&buffer[count])) ++count;
        if (count) {
            if (!paging_copy_to_user(space, address, buffer, count)) return error(RUM_EFAULT);
            return (rum_result_t)count;
        }
        if (!task_wait(event, observed)) return error(RUM_EIO);
        task_cancel_current_if_requested();
    }
}

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
        frame->eax = (uint32_t)console_read(task_current_process_space(), frame->ebx,
                                           frame->ecx, frame->edx);
        return;
    case RUM_SYS_WRITE:
        frame->eax = (uint32_t)console_write(task_current_process_space(), frame->ebx,
                                            frame->ecx, frame->edx);
        return;
    default:
        frame->eax = (uint32_t)-RUM_ENOSYS;
        return;
    }
}
