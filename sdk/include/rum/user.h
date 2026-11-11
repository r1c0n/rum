#ifndef RUM_USER_H
#define RUM_USER_H

#include <rum/abi/error.h>
#include <rum/abi/process.h>
#include <rum/abi/syscall.h>
#include <rum/abi/filesystem.h>
#include <rum/abi/console.h>
#include <rum/abi/time.h>
#include <rum/abi/pipe.h>
#include <rum/abi/job.h>
#include <rum/abi/terminal.h>
#include <rum/abi/memory.h>
#include <rum/abi/object.h>
#include <rum/abi/process-object.h>
rum_result_t rum_process_object_control(struct rum_process_object_request *);
rum_result_t rum_process_open(rum_pid_t, uint32_t rights, uint32_t flags, rum_object_handle_t *);
rum_result_t rum_process_query(rum_object_handle_t, struct rum_process_object_information *);
rum_result_t rum_process_wait(rum_object_handle_t, uint32_t flags, struct rum_process_object_information *);
rum_result_t rum_process_terminate(rum_object_handle_t);
rum_result_t rum_object_control(struct rum_object_request *);
rum_result_t rum_object_query(rum_object_handle_t, uint32_t expected_type, struct rum_object_information *);
rum_result_t rum_object_duplicate(rum_object_handle_t, uint32_t expected_type, uint32_t rights,
                                   uint32_t flags, rum_object_handle_t *);
rum_result_t rum_object_close(rum_object_handle_t);
rum_result_t rum_object_from_descriptor(rum_handle_t, uint32_t expected_type, uint32_t rights,
                                       uint32_t flags, rum_object_handle_t *);
rum_result_t rum_object_io(struct rum_object_io_request *);
rum_result_t rum_object_read(rum_object_handle_t, void *, rum_size_t);
rum_result_t rum_object_write(rum_object_handle_t, const void *, rum_size_t);
rum_result_t rum_object_seek(rum_object_handle_t, int64_t displacement, uint32_t whence, uint64_t *position);
rum_result_t rum_object_readdir(rum_object_handle_t, struct rum_directory_entry *);
rum_result_t rum_object_flush(rum_object_handle_t);
rum_result_t rum_object_isatty(rum_object_handle_t);
/* Packets carry version/size; allocate returns its chosen address/rounded size.
   Both calls return zero or a negative public error, never a signed address. */
rum_result_t rum_vm_allocate(struct rum_vm_allocate_request *);
rum_result_t rum_vm_release(rum_address_t address, rum_size_t bytes);
rum_result_t rum_vm_protect(rum_address_t address, rum_size_t bytes, uint32_t protection);
rum_result_t rum_terminal_query(struct rum_terminal_capabilities *);
rum_result_t rum_terminal_begin(void);
rum_result_t rum_terminal_begin_inline(void);
rum_result_t rum_terminal_scroll(uint32_t direction);
rum_result_t rum_terminal_line(const char *, uint32_t bytes, uint32_t cursor);
rum_result_t rum_terminal_styled_line(const struct rum_terminal_cell *, uint32_t count, uint32_t cursor);
rum_result_t rum_terminal_end(void);
rum_result_t rum_terminal_cells(uint32_t x, uint32_t y, const struct rum_terminal_cell *, uint32_t count);
rum_result_t rum_terminal_cursor(uint32_t x, uint32_t y, uint32_t visible);
rum_result_t rum_terminal_color(uint32_t foreground, uint32_t background);
rum_result_t rum_terminal_read(struct rum_terminal_event *, uint32_t capacity, uint32_t milliseconds, uint32_t *ticks);

rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags);
rum_result_t rum_stat(const char *, struct rum_file_information *);
rum_result_t rum_fstat(rum_handle_t, struct rum_file_information *);
rum_result_t rum_rename(const char *, const char *, uint32_t flags);
rum_result_t rum_close(rum_handle_t);
rum_result_t rum_isatty(rum_handle_t);
rum_result_t rum_job_create(const char *, uint32_t flags);
rum_result_t rum_job_commit(uint32_t);
rum_result_t rum_job_cancel(uint32_t);
rum_result_t rum_job_list(struct rum_job_entry *, uint32_t capacity, uint32_t *total);
rum_result_t rum_job_wait(uint32_t, uint32_t flags, struct rum_job_entry *);
rum_result_t rum_job_foreground(uint32_t);
rum_result_t rum_job_release(uint32_t);
rum_result_t rum_job_ack(uint32_t);
rum_result_t rum_pipe(struct rum_pipe_request *);
rum_result_t rum_duplicate(rum_handle_t source, rum_handle_t target, uint32_t flags);
rum_result_t rum_seek(rum_handle_t, int64_t displacement, uint32_t whence, uint64_t *position);
rum_result_t rum_readdir(rum_handle_t, struct rum_directory_entry *);
rum_result_t rum_chdir(const char *);
rum_result_t rum_getcwd(char *, rum_size_t capacity);
rum_result_t rum_mkdir(const char *);
rum_result_t rum_remove(const char *);
rum_result_t rum_flush(rum_handle_t);
rum_result_t rum_run(const char *, const struct rum_arguments *, struct rum_process_result *);
rum_result_t rum_spawn(const char *, const struct rum_arguments *);
rum_result_t rum_spawn_with_handles(const char *, const struct rum_arguments *,
                                   const struct rum_handle_mapping *, uint32_t count, uint32_t flags);
rum_result_t rum_spawn_with_objects(const char *, const struct rum_arguments *,
                                   const struct rum_handle_mapping *, uint32_t count, uint32_t flags,
                                   const struct rum_object_inheritance *, uint32_t object_count);
rum_result_t rum_wait(rum_pid_t, uint32_t flags, struct rum_process_result *);
rum_result_t rum_terminate(rum_pid_t, uint32_t flags);
rum_result_t rum_yield(void);
rum_result_t rum_sleep(uint32_t milliseconds);
rum_result_t rum_clock(struct rum_clock_request *);
rum_result_t rum_monotonic_ms(uint64_t *milliseconds);
rum_result_t rum_process_list(struct rum_process_entry *, uint32_t capacity, uint32_t *total);
rum_result_t rum_replace(const char *, const void *, rum_size_t);
rum_result_t rum_console(uint32_t);
rum_result_t rum_run_command(const char *, const struct rum_arguments *, const char *, struct rum_process_result *);
rum_result_t rum_run_argv_command(const char *, const struct rum_arguments *, struct rum_process_result *);
rum_result_t rum_command_text(char *, rum_size_t);
rum_result_t rum_session(uint32_t action, const char *path);

rum_result_t rum_syscall3(uint32_t number, uint32_t first, uint32_t second, uint32_t third);
rum_result_t rum_read(rum_handle_t handle, void *buffer, rum_size_t capacity);
rum_result_t rum_write(rum_handle_t handle, const void *buffer, rum_size_t bytes);
rum_result_t rum_getpid(void);
_Noreturn void rum_exit(int32_t status);

#endif
