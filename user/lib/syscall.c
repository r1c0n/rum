#include <rum/user.h>

_Static_assert(sizeof(void *) == sizeof(rum_address_t), "i386 user pointers");

rum_result_t rum_read(rum_handle_t handle, void *buffer, rum_size_t capacity)
{
    return rum_syscall3(RUM_SYS_READ, handle, (rum_address_t)(uintptr_t)buffer, capacity);
}

rum_result_t rum_write(rum_handle_t handle, const void *buffer, rum_size_t bytes)
{
    return rum_syscall3(RUM_SYS_WRITE, handle, (rum_address_t)(uintptr_t)buffer, bytes);
}

rum_result_t rum_getpid(void)
{
    return rum_syscall3(RUM_SYS_GETPID, 0, 0, 0);
}

_Noreturn void rum_exit(int32_t status)
{
    (void)rum_syscall3(RUM_SYS_EXIT, (uint32_t)status, 0, 0);
    /* A returning exit violates the ABI. Fault in user mode rather than use
       a privileged halt instruction or silently continue past main. */
    for (;;) __asm__ volatile ("ud2");
}
