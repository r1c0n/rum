#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/pipe.h>
#include <rum/task.h>

struct kernel_pipe {
    bool allocated, reader, writer;
    uint32_t head, bytes, operations, read_waiters, write_waiters;
    struct task_event readable, writable;
    unsigned char data[RUM_PIPE_CAPACITY];
};
static struct kernel_pipe pipes[RUM_KERNEL_PIPE_LIMIT];
_Static_assert(RUM_PIPE_ATOMIC_WRITE > 0 && RUM_PIPE_ATOMIC_WRITE <= RUM_PIPE_CAPACITY,
               "atomic chunks fit inside a bounded pipe");

static bool task_context(void)
{
    if (irq_in_handler()) return false;
    uint32_t flags = cpu_interrupt_save(); cpu_interrupt_restore(flags);
    return (flags & 0x200u) != 0;
}
/* IF clear. An outstanding operation keeps event storage alive even after
   both endpoint descriptions close. No task can then observe a reused slot. */
static void reclaim(struct kernel_pipe *pipe)
{
    if (!pipe->reader && !pipe->writer && !pipe->operations) pipe->allocated = false;
}
struct kernel_pipe *pipe_create(void)
{
    if (!task_context()) return NULL;
    uint32_t flags = cpu_interrupt_save();
    struct kernel_pipe *result = NULL;
    for (unsigned i = 0; i < RUM_KERNEL_PIPE_LIMIT; ++i) if (!pipes[i].allocated) {
        result = &pipes[i];
        result->allocated = result->reader = result->writer = true;
        result->head = result->bytes = result->operations = 0;
        result->read_waiters = result->write_waiters = 0;
        result->readable = result->writable = (struct task_event){0};
        /* Old bytes are inaccessible: reads copy only the occupied range,
           populated by writes after this count/head reset. */
        break;
    }
    cpu_interrupt_restore(flags);
    return result;
}
bool pipe_close(struct kernel_pipe *pipe, bool writer)
{
    if (!pipe || irq_in_handler()) return false;
    uint32_t flags = cpu_interrupt_save();
    bool valid = pipe->allocated && (writer ? pipe->writer : pipe->reader);
    if (valid) {
        if (writer) { pipe->writer = false; task_event_signal(&pipe->readable); }
        else { pipe->reader = false; task_event_signal(&pipe->writable); }
        reclaim(pipe);
    }
    cpu_interrupt_restore(flags);
    return valid;
}
struct pipe_statistics pipe_stats(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct pipe_statistics result = {0};
    for (unsigned i = 0; i < RUM_KERNEL_PIPE_LIMIT; ++i) if (pipes[i].allocated) {
        ++result.pipes; result.readers += pipes[i].reader; result.writers += pipes[i].writer;
        result.buffered_bytes += pipes[i].bytes; result.operations += pipes[i].operations;
        result.read_waiters += pipes[i].read_waiters; result.write_waiters += pipes[i].write_waiters;
    }
    cpu_interrupt_restore(flags); return result;
}
rum_result_t pipe_try_read(struct kernel_pipe *pipe, void *buffer, size_t bytes)
{
    if (!pipe || !pipe->allocated || !pipe->reader) return -RUM_EBADF;
    if (!bytes) return 0;
    if (!buffer) return -RUM_EINVAL;
    if (!task_context()) return -RUM_EBUSY;
    uint32_t flags = cpu_interrupt_save();
    size_t amount = bytes < pipe->bytes ? bytes : pipe->bytes;
    if (amount > RUM_PIPE_ATOMIC_WRITE) amount = RUM_PIPE_ATOMIC_WRITE;
    if (!amount) {
        rum_result_t result = pipe->writer ? PIPE_WOULD_BLOCK : 0;
        cpu_interrupt_restore(flags); return result;
    }
    size_t first = RUM_PIPE_CAPACITY - pipe->head;
    if (first > amount) first = amount;
    memcpy(buffer, pipe->data + pipe->head, first);
    memcpy((unsigned char *)buffer + first, pipe->data, amount - first);
    pipe->head = (pipe->head + amount) % RUM_PIPE_CAPACITY;
    pipe->bytes -= amount;
    task_event_signal(&pipe->writable);
    cpu_interrupt_restore(flags); return (rum_result_t)amount;
}
rum_result_t pipe_try_write(struct kernel_pipe *pipe, const void *buffer, size_t bytes)
{
    if (!pipe || !pipe->allocated || !pipe->writer) return -RUM_EBADF;
    if (!bytes) return 0;
    if (!buffer) return -RUM_EINVAL;
    if (!task_context()) return -RUM_EBUSY;
    uint32_t flags = cpu_interrupt_save();
    if (!pipe->reader) { cpu_interrupt_restore(flags); return -RUM_EPIPE; }
    size_t amount = bytes < RUM_PIPE_ATOMIC_WRITE ? bytes : RUM_PIPE_ATOMIC_WRITE;
    if (amount > RUM_PIPE_CAPACITY - pipe->bytes) { cpu_interrupt_restore(flags); return PIPE_WOULD_BLOCK; }
    size_t tail = (pipe->head + pipe->bytes) % RUM_PIPE_CAPACITY;
    size_t first = RUM_PIPE_CAPACITY - tail;
    if (first > amount) first = amount;
    memcpy(pipe->data + tail, buffer, first);
    memcpy(pipe->data, (const unsigned char *)buffer + first, amount - first);
    pipe->bytes += amount;
    task_event_signal(&pipe->readable);
    cpu_interrupt_restore(flags); return (rum_result_t)amount;
}
static rum_result_t transfer(struct kernel_pipe *pipe, void *buffer, size_t bytes, bool write)
{
    if (!pipe || !pipe->allocated) return -RUM_EBADF;
    if (!bytes) return 0;
    if (!buffer) return -RUM_EINVAL;
    if (!task_context()) return -RUM_EBUSY;
    uint32_t flags = cpu_interrupt_save(); ++pipe->operations; cpu_interrupt_restore(flags);
    rum_result_t result;
    for (;;) {
        if (task_current_cancelled()) { result = -RUM_EINTR; break; }
        struct task_event *event = write ? &pipe->writable : &pipe->readable;
        uint32_t observed = task_event_sequence(event);
        result = write ? pipe_try_write(pipe, buffer, bytes) : pipe_try_read(pipe, buffer, bytes);
        if (result != PIPE_WOULD_BLOCK) break;
        flags = cpu_interrupt_save();
        if (write) ++pipe->write_waiters; else ++pipe->read_waiters;
        cpu_interrupt_restore(flags);
        bool woke = task_wait(event, observed);
        flags = cpu_interrupt_save();
        if (write) --pipe->write_waiters; else --pipe->read_waiters;
        cpu_interrupt_restore(flags);
        if (!woke) { result = -RUM_EBUSY; break; }
    }
    flags = cpu_interrupt_save(); --pipe->operations; reclaim(pipe); cpu_interrupt_restore(flags);
    return result;
}
rum_result_t pipe_read(struct kernel_pipe *pipe, void *buffer, size_t bytes)
{
    return transfer(pipe, buffer, bytes, false);
}
rum_result_t pipe_write(struct kernel_pipe *pipe, const void *buffer, size_t bytes)
{
    return transfer(pipe, (void *)buffer, bytes, true);
}
