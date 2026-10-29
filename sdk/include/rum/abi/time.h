#ifndef RUM_ABI_TIME_H
#define RUM_ABI_TIME_H

#include <rum/abi/types.h>

#define RUM_SLEEP_ABI_VERSION 1
#define RUM_SLEEP_MAX_MS 86400000u /* 24 hours; larger requests return ERANGE. */

#ifndef __ASSEMBLER__
/* Relative milliseconds. Zero yields without arming a deadline. Positive
   durations round up to PIT ticks plus one tick to cover the initial phase.
   flags/reserved and unused syscall registers must be zero. */
struct rum_sleep_request {
    uint32_t version, size, milliseconds, flags, reserved[2];
};
_Static_assert(sizeof(struct rum_sleep_request) == 24, "sleep packet layout");
#endif
#endif
