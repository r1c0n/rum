#ifndef RUM_USER_H
#define RUM_USER_H

#include <rum/abi/error.h>
#include <rum/abi/process.h>
#include <rum/abi/syscall.h>

rum_result_t rum_syscall3(uint32_t number, uint32_t first, uint32_t second, uint32_t third);
rum_result_t rum_read(rum_handle_t handle, void *buffer, rum_size_t capacity);
rum_result_t rum_write(rum_handle_t handle, const void *buffer, rum_size_t bytes);
rum_result_t rum_getpid(void);
_Noreturn void rum_exit(int32_t status);

#endif
