#ifndef RUM_ABI_PIPE_H
#define RUM_ABI_PIPE_H
#include <rum/abi/types.h>

#define RUM_PIPE_ABI_VERSION 1
#define RUM_PIPE_CAPACITY 4096u
#define RUM_PIPE_ATOMIC_WRITE 512u

#ifndef __ASSEMBLER__
/* Input: exact version/size; all other words zero. Success fills two distinct
   descriptors and the limits. Failure leaves the packet and table unchanged. */
struct rum_pipe_request {
    uint32_t version, size, flags, reserved;
    rum_handle_t read_handle, write_handle;
    uint32_t capacity, atomic_write;
};
_Static_assert(sizeof(struct rum_pipe_request) == 32, "pipe packet layout");
#endif
#endif
