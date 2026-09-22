#ifndef RUM_PROCESS_H
#define RUM_PROCESS_H

#include <rum/task.h>

enum process_launch_error {
    PROCESS_LAUNCH_OK,
    PROCESS_LAUNCH_INVALID_ARGUMENTS,
    PROCESS_LAUNCH_EXECUTABLE,
    PROCESS_LAUNCH_RESOURCES,
    PROCESS_LAUNCH_INTERNAL,
};

struct process_result {
    rum_pid_t process_id;
    enum task_termination termination;
    rum_result_t exit_status;
    struct task_fault fault;
};

/* Load one RAM-file ELF, run it as this task's foreground child, wait for its
   complete result, and release every owned task and address-space resource. */
enum process_launch_error process_launch_foreground(const char *program,
                                                     const char *arguments,
                                                     struct process_result *result);

#endif
