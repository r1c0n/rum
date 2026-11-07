#ifndef RUM_PIPE_H
#define RUM_PIPE_H
#include <rum/abi/pipe.h>
#include <stdbool.h>
#include <stddef.h>

#define RUM_KERNEL_PIPE_LIMIT 32u
/* Private try-operation result; blocking wrappers never expose it to users. */
#define PIPE_WOULD_BLOCK INT32_MIN
struct kernel_pipe;
struct pipe_statistics {
    uint32_t pipes, readers, writers, buffered_bytes, operations, read_waiters, write_waiters;
};

/* One read and one write endpoint owned by their open descriptions. Duplicates
   share those descriptions; close an endpoint only after its final alias. */
struct kernel_pipe *pipe_create(void);
bool pipe_close(struct kernel_pipe *, bool writer);
struct pipe_statistics pipe_stats(void);
/* Trusted kernel buffers. Try operations are bounded, allocation-free and
   never sleep. Writes commit one complete chunk of up to ATOMIC_WRITE bytes. */
rum_result_t pipe_try_read(struct kernel_pipe *, void *, size_t);
rum_result_t pipe_try_write(struct kernel_pipe *, const void *, size_t);
/* Task context only; predicate/sequence waits are cancellable. An active call
   pins its pipe and event storage until it unwinds, including cancellation. */
rum_result_t pipe_read(struct kernel_pipe *, void *, size_t);
rum_result_t pipe_write(struct kernel_pipe *, const void *, size_t);
#endif
