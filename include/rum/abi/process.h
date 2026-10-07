#ifndef RUM_ABI_PROCESS_H
#define RUM_ABI_PROCESS_H

#include <rum/abi/types.h>

#define RUM_ABI_ARGUMENT_LIMIT 32 /* Includes argv[0]. */
#define RUM_ABI_ARGUMENT_BYTES 4096 /* Includes every string's NUL. */
#define RUM_PROCESS_ABI_VERSION 1
#define RUM_PROCESS_EXITED 1
#define RUM_PROCESS_FAULTED 2
#define RUM_PROCESS_CANCELLED 3
#define RUM_SHELL_RECOVERY_STATUS 75 /* Initial shell's explicit recovery request. */

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

/* Synchronous foreground launch. All addresses are copied/validated before
   loading. flags/reserved are zero; only the calling process can wait/reap. */
struct rum_run_request {
    uint32_t version, size;
    rum_address_t path, arguments, result;
    uint32_t flags, reserved[2];
};
/* Initialize version, size and reserved. Other words are output only. Faults
   expose a vector, never an instruction, stack, kernel or physical address. */
struct rum_process_result {
    uint32_t version, size;
    rum_pid_t process_id;
    uint32_t termination;
    int32_t status;
    uint32_t fault_vector, reserved;
};
_Static_assert(sizeof(struct rum_run_request) == 32 && sizeof(struct rum_process_result) == 28,
               "process packet layout");

_Static_assert(offsetof(struct rum_arguments, offsets) == 8 &&
               offsetof(struct rum_arguments, strings) == 136 &&
               sizeof(struct rum_arguments) == 4232, "argument packet layout");
#endif
#endif
