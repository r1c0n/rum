#ifndef RUM_ABI_TYPES_H
#define RUM_ABI_TYPES_H

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Wire values are 32-bit even when inspected by a 64-bit host tool. */
typedef uint32_t rum_address_t;
typedef uint32_t rum_size_t;
typedef uint32_t rum_pid_t;
typedef uint32_t rum_handle_t;
typedef int32_t rum_result_t;

_Static_assert(sizeof(rum_address_t) == 4 && sizeof(rum_result_t) == 4,
               "32-bit user ABI");
#endif
#endif
