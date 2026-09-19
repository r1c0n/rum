#ifndef RUM_ABI_PROCESS_H
#define RUM_ABI_PROCESS_H

#include <rum/abi/types.h>

#define RUM_ABI_ARGUMENT_LIMIT 32 /* Includes argv[0]. */
#define RUM_ABI_ARGUMENT_BYTES 4096 /* Includes every string's NUL. */

#ifndef __ASSEMBLER__
#include <stddef.h>

/* Launch packet: copied as bounded bytes, never followed as host/kernel
   pointers. argc is 1..32; offsets describe tightly packed strings in order,
   beginning at offset zero. string_bytes counts exactly those strings. */
struct rum_arguments {
    uint32_t argc;
    uint32_t string_bytes;
    uint32_t offsets[RUM_ABI_ARGUMENT_LIMIT];
    char strings[RUM_ABI_ARGUMENT_BYTES];
};

_Static_assert(offsetof(struct rum_arguments, offsets) == 8 &&
               offsetof(struct rum_arguments, strings) == 136 &&
               sizeof(struct rum_arguments) == 4232, "argument packet layout");
#endif
#endif
