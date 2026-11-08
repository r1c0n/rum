#ifndef RUM_ABI_TERMINAL_H
#define RUM_ABI_TERMINAL_H
#include <rum/abi/types.h>
#define RUM_TERMINAL_ABI_VERSION 1u
#define RUM_TERM_QUERY 1u
#define RUM_TERM_BEGIN 2u
#define RUM_TERM_END 3u
#define RUM_TERM_CELLS 4u
#define RUM_TERM_CURSOR 5u
#define RUM_TERM_COLOR 6u
#define RUM_TERM_READ 7u
#define RUM_TERM_MAX_CELLS 80u
#define RUM_TERM_MAX_EVENTS 32u
#define RUM_TERM_MAX_WAIT_MS 60000u
#define RUM_TERM_TEXT_EVENT 1u
#define RUM_TERM_FEATURE_CELLS 1u
#define RUM_TERM_FEATURE_CURSOR 2u
#define RUM_TERM_FEATURE_COLOR 4u
#define RUM_TERM_FEATURE_CHARACTERS 8u
#define RUM_TERM_FEATURE_TIMED_INPUT 16u
#define RUM_TERM_FEATURES 31u
#define RUM_TERM_ACTIVE 1u
#define RUM_TERM_OWNED 2u
#define RUM_COLOR_BLACK 0u
#define RUM_COLOR_BLUE 1u
#define RUM_COLOR_GREEN 2u
#define RUM_COLOR_CYAN 3u
#define RUM_COLOR_RED 4u
#define RUM_COLOR_MAGENTA 5u
#define RUM_COLOR_BROWN 6u
#define RUM_COLOR_LIGHT_GREY 7u
#define RUM_COLOR_DARK_GREY 8u
#define RUM_COLOR_LIGHT_BLUE 9u
#define RUM_COLOR_LIGHT_GREEN 10u
#define RUM_COLOR_LIGHT_CYAN 11u
#define RUM_COLOR_LIGHT_RED 12u
#define RUM_COLOR_LIGHT_MAGENTA 13u
#define RUM_COLOR_YELLOW 14u
#define RUM_COLOR_WHITE 15u
#ifndef __ASSEMBLER__
/* QUERY is output only. Geometry excludes the reserved system status row.
   ticks is a wrapping observation counter, not a wall clock. */
struct rum_terminal_capabilities {
    uint32_t version, size, width, height, max_cells, max_events, tick_hz, ticks;
    uint32_t features, mode, cursor_x, cursor_y, foreground, background, cursor_visible;
};
struct rum_terminal_cell { uint8_t character, foreground, background, reserved; };
/* Decoded character press; tick records dequeue time, not hardware arrival.
   Ctrl+C follows session policy: foreground children are cancelled; the
   supervised shell receives value 3 instead of being terminated. */
struct rum_terminal_event { uint32_t type, value, tick, reserved; };
/* All unused fields, flags, now_ticks and reserved words must be zero.
   CURSOR: x/y and flags 0/1 for visibility. COLOR: x/y are colors 0..15.
   CELLS: x/y and one row's count. READ: event capacity and timeout_ms.
   QUERY/READ require writable output storage disjoint from the request.
   Every success fills now_ticks; errors leave user outputs unchanged. */
struct rum_terminal_request {
    uint32_t version, size, operation, flags, x, y, count, timeout_ms;
    rum_address_t data;
    uint32_t now_ticks, reserved[2];
};
_Static_assert(sizeof(struct rum_terminal_request) == 48 &&
               sizeof(struct rum_terminal_capabilities) == 60 &&
               sizeof(struct rum_terminal_cell) == 4 && sizeof(struct rum_terminal_event) == 16,
               "terminal ABI layout");
#endif
#endif
