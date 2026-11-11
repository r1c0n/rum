# Pipes and stream I/O

Pipes connect programs through a bounded byte stream. Include `<rum/user.h>`
and use `rum_pipe` to obtain separate read and write descriptors. They work
with `rum_read`, `rum_write`, duplication and explicit child mappings.

Endpoints can also be exported with `rum_object_from_descriptor` and used with
`rum_object_read` / `rum_object_write`. A typed alias retains the same endpoint;
closing the last descriptor does not signal EOF or EPIPE while that alias or an
active operation still owns it. The final common reference closes the endpoint
once. Buffer, atomic-write, cancellation and foreground rules are unchanged.

## Create and use a pipe

```c
struct rum_pipe_request pipe = {
    .version = RUM_PIPE_ABI_VERSION, .size = sizeof pipe
};
if (rum_pipe(&pipe) < 0) return 1;
rum_result_t written = rum_write(pipe.write_handle, "rum", 3);
rum_close(pipe.write_handle);
char bytes[8];
rum_result_t read = rum_read(pipe.read_handle, bytes, sizeof bytes);
rum_result_t end = rum_read(pipe.read_handle, bytes, sizeof bytes);
rum_close(pipe.read_handle);
return written == 3 && read == 3 && end == 0 ? 0 : 1;
```

The request is 32 bytes: version, size, flags, reserved, read_handle,
write_handle, capacity and atomic_write, all 32-bit words. Initialize only
version and size; every other field must be zero. Syscall 26 returns zero and
fills both handles and the limits. Failure leaves the packet and descriptor
table unchanged. A packet may cross writable user pages, but must be writable
throughout. Reusing a successful packet requires clearing its output fields.

The kernel allocates the lowest two free slots in 3–31. With fresh standard
streams, a process can create 14 pipes before its descriptor table fills.
One free slot is insufficient: creation returns `-RUM_EMFILE` without keeping
an endpoint. Global pipe/description pool exhaustion returns `-RUM_ENOMEM`.
The current kernel supports 32 live pipes, each with a 4,096-byte buffer.

## Reads, writes and atomicity

- Read returns available bytes, up to the requested count and 512-byte syscall
  limit. On an empty pipe it waits while a writer remains.
- EOF is zero only after the final writer closes and buffered data is consumed.
- Write accepts one complete chunk of up to 512 bytes. It waits until enough
  space exists for that chunk while a reader remains. A partially filled pipe
  can therefore block an atomic write that does not fit yet.
- A request larger than 512 bytes returns a short count of at most 512; loop
  over the remainder. Another writer may run between calls.
- A nonempty write without readers returns `-RUM_EPIPE`. It does not deliver a
  signal or terminate the caller automatically.
- Zero-length transfers return zero after validating the descriptor and
  direction. They ignore the buffer, do not wait, and do not consume data.
  This remains true after peer closure.

`RUM_PIPE_CAPACITY` and `RUM_PIPE_ATOMIC_WRITE` describe these limits; successful
creation also returns them in the packet. Writes of at most the atomic limit
are indivisible with respect to other writers. Each accepted larger-request
chunk is also indivisible, but an entire multi-call operation is not atomic.

The stream preserves committed byte order. Competing readers divide the data;
there is no broadcast. Competing callers have no guaranteed arrival order or
message boundaries. Use an application framing format if records matter.

The kernel validates the complete nonempty user buffer before touching the
queue, even when it will transfer only one chunk. Invalid ranges return
`-RUM_EFAULT` without consuming or appending bytes. Pipe data never uses a
file offset. Seeking either end returns `-RUM_ESPIPE`, directory listing
`-RUM_ENOTDIR`, and flush `-RUM_EINVAL`. The wrong transfer direction returns
`-RUM_EBADF`, including zero-byte requests.

## Inheritance and endpoint closure

Duplicates and mapped descriptors share an open description. Closing one alias
keeps the endpoint open while another alias exists. EOF requires every write
alias to close; broken-pipe detection requires every read alias to close.
Process exit, user faults and cancellation close endpoints through the same
reference-counted handle cleanup.

Use [`rum_spawn_with_handles`](shared-handles.md) to place a read end at child
stdin or a write end at child stdout/stderr. Only explicitly selected sources
are inherited, subject to the descriptor's CLOSPAWN policy. Pipe I/O does not
require foreground keyboard ownership and does not grant console-control rights.

Launch cooperating programs before waiting for them, then close each unused
parent endpoint. A parent retaining a write alias prevents EOF. A retained read
alias prevents broken-pipe detection. Writing an entire large payload before
starting a reader can block when the bounded buffer fills.

The shell uses these APIs for [`|` and file redirection](shell-workflows.md).
They are also available directly to user programs.

## Waiting, cancellation and lifetime

Each pipe has readable and writable events. Transfers and final endpoint
closure wake only relevant pipe waiters. Waiters capture a sequence before
checking the queue and recheck after waking. They do not poll on timer ticks.
Queue updates copy at most 512 bytes under a bounded interrupt-protected update;
waiting occurs afterward in task context with no filesystem or block ownership.

Cancellation wakes a blocked call, which releases its waiter registration and
active-operation reference before returning. The normal syscall-return boundary
then completes process cancellation and closes its descriptors. Bytes already
committed by an earlier operation remain committed.

Endpoints and active operations retain the pipe. Event storage cannot be reused
while a suspended call still refers to it. The fixed pool needs no per-transfer
heap allocation. This implementation relies on rum's single CPU and cooperative
kernel execution; SMP and multiple threads per process need a later locking audit.

## Diagnostics and focused checks

Recovery `diag` reports live pipes, buffered bytes, read/write ends, active
operations and waiters. End counts represent open endpoint descriptions, rather
than the number of descriptor aliases. All counts reach zero after userspace
and its children are reclaimed.

```powershell
.\rum.ps1 test -Suite pipes -Jobs 4
```

```sh
./rum test --suite pipes -j4
make test-pipes-host  # Bounded FIFO and ownership checks without QEMU.
```

Host tests check wraparound, exact bytes, atomic writes, zero-length requests,
sequence races, partial allocation rollback, pool limits and active-operation
lifetime. Ring-3 tests use ELF/16 MiB and ISO/64 MiB boots for payloads larger
than capacity, several readers/writers, standard-stream inheritance, last-alias
closure, faults, blocked-call cancellation, parent exit and resource baselines.
Logs are saved under `build/test-artifacts/pipes/`. Tests use no working disk.
