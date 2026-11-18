#ifndef RUM_TERMINAL_SESSION_H
#define RUM_TERMINAL_SESSION_H
#include <rum/abi/terminal.h>
#include <rum/abi/types.h>
#include <stdbool.h>
/* Task context, IF enabled. No allocation or waits. Snapshot excludes status. */
rum_result_t terminal_session_begin(uint32_t thread, uint32_t member);
rum_result_t terminal_session_begin_inline(uint32_t thread, uint32_t member);
rum_result_t terminal_input_begin(uint32_t thread, uint32_t member);
bool terminal_input_owned(uint32_t thread);
void terminal_input_end(uint32_t thread);
bool terminal_session_inline(void);
bool terminal_session_redraw(void);
void terminal_session_line(const char *, uint32_t bytes, uint32_t cursor);
void terminal_session_styled_line(const struct rum_terminal_cell *, uint32_t count, uint32_t cursor);
rum_result_t terminal_session_end(uint32_t owner);
bool terminal_session_owned(uint32_t owner);
uint32_t terminal_session_owner(void);
void terminal_session_reconcile(void);
void terminal_session_exit(uint32_t owner);
bool terminal_session_output(uint32_t task);
#endif
