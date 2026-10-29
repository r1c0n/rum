#ifndef RUM_USER_H
#define RUM_USER_H

#include <rum/abi/error.h>
#include <rum/abi/process.h>
#include <rum/abi/syscall.h>
#include <rum/abi/filesystem.h>
#include <rum/abi/console.h>

rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags);
rum_result_t rum_close(rum_handle_t);
rum_result_t rum_seek(rum_handle_t, int64_t displacement, uint32_t whence, uint64_t *position);
rum_result_t rum_readdir(rum_handle_t, struct rum_directory_entry *);
rum_result_t rum_chdir(const char *);
rum_result_t rum_getcwd(char *, rum_size_t capacity);
rum_result_t rum_mkdir(const char *);
rum_result_t rum_remove(const char *);
rum_result_t rum_flush(rum_handle_t);
rum_result_t rum_run(const char *, const struct rum_arguments *, struct rum_process_result *);
rum_result_t rum_replace(const char *, const void *, rum_size_t);
rum_result_t rum_console(uint32_t);
rum_result_t rum_run_command(const char *, const struct rum_arguments *, const char *, struct rum_process_result *);
rum_result_t rum_command_text(char *, rum_size_t);
rum_result_t rum_session(uint32_t action, const char *path);

rum_result_t rum_syscall3(uint32_t number, uint32_t first, uint32_t second, uint32_t third);
rum_result_t rum_read(rum_handle_t handle, void *buffer, rum_size_t capacity);
rum_result_t rum_write(rum_handle_t handle, const void *buffer, rum_size_t bytes);
rum_result_t rum_getpid(void);
_Noreturn void rum_exit(int32_t status);

#endif
