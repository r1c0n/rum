#ifndef RUM_TERMINAL_SESSION_H
#define RUM_TERMINAL_SESSION_H
#include <rum/abi/terminal.h>
#include <rum/abi/types.h>
#include <stdbool.h>
/* Task context, IF enabled. No allocation or waits. Snapshot excludes status. */
rum_result_t terminal_session_begin(uint32_t owner);
rum_result_t terminal_session_end(uint32_t owner);
bool terminal_session_owned(uint32_t owner);
uint32_t terminal_session_owner(void);
void terminal_session_reconcile(void);
void terminal_session_exit(uint32_t owner);
bool terminal_session_output(uint32_t task);
#endif
