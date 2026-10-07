#include <rum/user.h>

_Static_assert(sizeof(void *) == sizeof(rum_address_t), "i386 user pointers");

rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    struct rum_open_request request = { RUM_FS_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, access, flags, 0 };
    return rum_syscall3(RUM_SYS_OPEN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_close(rum_handle_t handle) { return rum_syscall3(RUM_SYS_CLOSE, handle, 0, 0); }
rum_result_t rum_seek(rum_handle_t handle, int64_t displacement, uint32_t whence, uint64_t *position)
{
    uint64_t bits = (uint64_t)displacement;
    struct rum_seek_request request = { RUM_FS_ABI_VERSION, sizeof request,
        (uint32_t)bits, (uint32_t)(bits >> 32), whence, 0, 0, 0 };
    rum_result_t result = rum_syscall3(RUM_SYS_SEEK, handle, (rum_address_t)(uintptr_t)&request, 0);
    if (!result && position) *position = ((uint64_t)request.position_hi << 32) | request.position_lo;
    return result;
}
rum_result_t rum_readdir(rum_handle_t handle, struct rum_directory_entry *entry)
{
    return rum_syscall3(RUM_SYS_READDIR, handle, (rum_address_t)(uintptr_t)entry, 0);
}
rum_result_t rum_chdir(const char *path) { return rum_syscall3(RUM_SYS_CHDIR, (rum_address_t)(uintptr_t)path, 0, 0); }
rum_result_t rum_getcwd(char *buffer, rum_size_t capacity) { return rum_syscall3(RUM_SYS_GETCWD, (rum_address_t)(uintptr_t)buffer, capacity, 0); }
rum_result_t rum_mkdir(const char *path) { return rum_syscall3(RUM_SYS_MKDIR, (rum_address_t)(uintptr_t)path, 0, 0); }
rum_result_t rum_remove(const char *path) { return rum_syscall3(RUM_SYS_REMOVE, (rum_address_t)(uintptr_t)path, 0, 0); }
rum_result_t rum_flush(rum_handle_t handle) { return rum_syscall3(RUM_SYS_FLUSH, handle, 0, 0); }
rum_result_t rum_run(const char *path, const struct rum_arguments *arguments, struct rum_process_result *result)
{
    struct rum_run_request request = { RUM_PROCESS_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)arguments,
        (rum_address_t)(uintptr_t)result, 0, {0, 0} };
    return rum_syscall3(RUM_SYS_RUN, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_replace(const char *path, const void *data, rum_size_t bytes)
{
    struct rum_replace_request request = { RUM_FS_ABI_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)path, (rum_address_t)(uintptr_t)data, bytes, 0 };
    return rum_syscall3(RUM_SYS_REPLACE, (rum_address_t)(uintptr_t)&request, 0, 0);
}
rum_result_t rum_console(uint32_t action) { return rum_syscall3(RUM_SYS_CONSOLE, action, 0, 0); }

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
