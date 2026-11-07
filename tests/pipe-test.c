#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/pipe.h>
#include "pipe-task-backend.h"

uint32_t handle_test_flags = 0x200u;
static bool in_irq;
bool irq_in_handler(void) { return in_irq; }
static struct kernel_pipe *current;
static unsigned char pattern[RUM_PIPE_CAPACITY], received[RUM_PIPE_CAPACITY];
static void write_at_wait(void) { assert(pipe_try_write(current, pattern, 13) == 13); }
static void drain_at_wait(void) { assert(pipe_try_read(current, received, RUM_PIPE_ATOMIC_WRITE) == RUM_PIPE_ATOMIC_WRITE); }
static void close_writer(void) { assert(pipe_close(current, true)); }
static void close_reader(void) { assert(pipe_close(current, false)); }
static void cancel_at_wait(void) { pipe_test_cancelled = true; }
static void close_both(void)
{
    assert(pipe_close(current, true) && pipe_close(current, false));
    struct pipe_statistics stats = pipe_stats();
    assert(stats.pipes == 1 && stats.operations == 1 && stats.read_waiters == 1);
}
static void drained(void)
{
    struct pipe_statistics stats = pipe_stats();
    assert(!stats.pipes && !stats.operations && !stats.read_waiters && !stats.write_waiters && !stats.buffered_bytes);
}
static void fill(struct kernel_pipe *pipe)
{
    for (unsigned i = 0; i < RUM_PIPE_CAPACITY; i += RUM_PIPE_ATOMIC_WRITE)
        assert(pipe_try_write(pipe, pattern + i, RUM_PIPE_ATOMIC_WRITE) == RUM_PIPE_ATOMIC_WRITE);
}
int main(void)
{
    for (unsigned i = 0; i < sizeof pattern; ++i) pattern[i] = (unsigned char)(i * 37u + i / 17u);
    current = pipe_create(); assert(current);
    assert(pipe_try_read(current, received, 1) == PIPE_WOULD_BLOCK);
    assert(pipe_try_write(current, NULL, 0) == 0 && pipe_try_read(current, NULL, 0) == 0);
    handle_test_flags = 0;
    assert(!pipe_create() && pipe_try_write(current, pattern, 1) == -RUM_EBUSY);
    handle_test_flags = 0x200u; in_irq = true;
    assert(!pipe_create() && pipe_try_read(current, received, 1) == -RUM_EBUSY && !pipe_close(current, true));
    in_irq = false;
    fill(current);
    assert(pipe_try_write(current, pattern, 1) == PIPE_WOULD_BLOCK);
    assert(pipe_try_read(current, received, 17) == 17);
    assert(!memcmp(received, pattern, 17));
    unsigned before = pipe_stats().buffered_bytes;
    assert(pipe_try_write(current, pattern, 18) == PIPE_WOULD_BLOCK && pipe_stats().buffered_bytes == before);
    assert(pipe_try_write(current, pattern, 17) == 17);
    unsigned offset = 0;
    while (offset < RUM_PIPE_CAPACITY) {
        rum_result_t count = pipe_try_read(current, received + offset, RUM_PIPE_CAPACITY - offset);
        assert(count > 0); offset += (unsigned)count;
    }
    for (unsigned i = 0; i < sizeof received; ++i) assert(received[i] == pattern[(i + 17) % RUM_PIPE_CAPACITY]);
    pipe_test_before_wait = write_at_wait;
    assert(pipe_read(current, received, 99) == 13 && !memcmp(received, pattern, 13));
    pipe_test_before_wait = cancel_at_wait;
    assert(pipe_read(current, received, 1) == -RUM_EINTR);
    pipe_test_cancelled = false;
    assert(!pipe_stats().operations && !pipe_stats().read_waiters);
    fill(current); pipe_test_before_wait = drain_at_wait;
    assert(pipe_write(current, pattern, RUM_PIPE_CAPACITY) == RUM_PIPE_ATOMIC_WRITE);
    pipe_test_before_wait = cancel_at_wait;
    assert(pipe_write(current, pattern, 1) == -RUM_EINTR);
    pipe_test_cancelled = false;
    pipe_test_before_wait = close_reader;
    assert(pipe_write(current, pattern, 1) == -RUM_EPIPE);
    assert(pipe_try_write(current, NULL, 0) == 0);
    assert(pipe_close(current, true)); drained();
    current = pipe_create(); assert(current);
    assert(pipe_try_write(current, pattern, 5) == 5 && pipe_close(current, true));
    assert(pipe_read(current, received, sizeof received) == 5 && !memcmp(received, pattern, 5));
    assert(pipe_read(current, received, 1) == 0 && pipe_close(current, false)); drained();
    current = pipe_create(); assert(current);
    pipe_test_before_wait = close_writer;
    assert(pipe_read(current, received, 1) == 0 && pipe_close(current, false)); drained();
    current = pipe_create(); assert(current);
    pipe_test_before_wait = close_both;
    assert(pipe_read(current, received, 1) == -RUM_EBADF); drained();
    struct kernel_pipe *all[RUM_KERNEL_PIPE_LIMIT];
    for (unsigned i = 0; i < RUM_KERNEL_PIPE_LIMIT; ++i) { all[i] = pipe_create(); assert(all[i]); }
    assert(!pipe_create());
    for (unsigned i = 0; i < RUM_KERNEL_PIPE_LIMIT; ++i) assert(pipe_close(all[i], false) && pipe_close(all[i], true));
    drained();
    puts("PASS: pipe FIFO/wraparound, atomic chunks, waits, EOF, broken pipes, cancellation and pool lifetime");
    return 0;
}
