#ifndef RUM_TEST_PIPE_TASK_BACKEND_H
#define RUM_TEST_PIPE_TASK_BACKEND_H
#include <stdbool.h>
extern bool pipe_test_cancelled;
extern void (*pipe_test_before_wait)(void);
#endif
