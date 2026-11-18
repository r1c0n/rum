# Threading contract for rum 0.9

rum currently launches one user thread per process. This page defines the
limits, ownership and wire formats for adding siblings. The kernel implements
the stack reservation and the synchronization service. Userspace thread and
synchronization calls remain planned.

## Capacity on a 16 MiB machine

Keep the 16 process/worker containers. Reserve 32 execution slots system-wide
for initial threads, sibling threads and kernel workers combined, with up to
four threads per process including its initial thread. Boot and idle have
permanent records outside that limit. Count constructing and retiring slots
against their limits until rollback or reclamation completes. A retained join
handle holds a completion record and consumes an object slot, independently
of execution capacity. Keep the foreground launch reserve across both pools.

| Resource | Reservation | Physical cost at capacity |
| --- | --- | --- |
| Kernel stacks | Idle at slot 0, execution slots 1–32, emergency at 33 | 34 × 16 KiB plus one shared 4 KiB page table |
| User stacks and TLS | Four descending slots per private space | At most 32 × (64 KiB stack + 4 KiB TLS) system-wide |
| Guards | One 4 KiB page below each stack, including kernel stacks | No frames |
| Private stack tables and directories | One stack-window table and one directory per process | Up to 16 × 8 KiB |
| Scheduler metadata | Up to 34 thread records and 18 process containers | Budget 256 bytes per execution record and 4 KiB per container |

The mapped stacks, TLS and page structures total **713 frames (2,852 KiB)** including
private directories and stack tables. The scheduler record budget adds at most
80.5 KiB. Boot's existing 16 KiB stack is part of the kernel image. These are
bounds on threading overhead. The kernel, page allocator, VM metadata, object
storage, executable mappings and application heaps also need RAM. Admission
must check actual free resources, the 16 MiB per-space commit quota and the
64-entry VM registry, and unwind partial construction on failure.

To reproduce the minimum-RAM check, build `build/tests/paging-ok.elf` and run
`python3 scripts/smoke-test.py --paging-only`. The 16 MiB guest maps all 34
kernel stacks, checks their guards through another page directory, reserves
the remaining user-side frame budget through the physical allocator and
returns every frame. The user portion of this check models the future mappings.

Slot zero keeps the released initial ESP and argument layout. For user slot
`i`, check `i < 4` before calculating `top = 0xc0000000 - i * 0x12000`.
The 64 KiB below top is the stack, the next page down is its guard, and the
page below the guard is reserved for TLS. All four slots fit in one PDE.
Stack and TLS pages are process-owned mappings with thread-specific lifetime.
Only kernel construction may install them. User VM operations must reject
their release or reprotection. The existing launcher maps only slot zero's stack.

## Owners and protection

| State | Owner and protection when threads share a process |
| --- | --- |
| PID, parent, name, child list/event, process reference, orphan flag, termination, status and fault | Process lifecycle registry. Publish the first terminal reason once. Parent identity follows the process even when its creating thread exits. |
| Address space, region registry, page tables, mapping lease count | Process VM. A thread owns its individual syscall lease while using user memory, including across waits. Disallow incompatible mapping changes until the last lease ends. |
| Descriptor slots/flags and typed slots/generation/rights | Process tables. Validate and retain under the table guard, then operate through the retained object. Duplicate and inherit only authorized grants. |
| CWD and directory pin | Process filesystem context. Resolve a relative path from a retained CWD snapshot. Swap the reference as a unit on chdir. |
| Offset, pending error, append, backend pin | Shared open description. Serialize offset selection through I/O completion. Serialize append placement across descriptions at the filesystem as well. |
| Jobs, foreground owner, console membership/event, root-shell role and delegated command text | Process/session registry. A thread owns only its active read or terminal lease. A sibling exit cannot close the session or transfer the foreground. |
| TID, run state, stacks, kernel continuation, entry arguments, registers, TLS, slice and reschedule flag | Thread. Scheduler guard protects publication, switch and retirement. Update current identity, CR3, TSS entry stack and future TLS selector as one switch. |
| Switch holds, event pointer/sequence, deadline, wake reason, active I/O reference and syscall scratch | Calling thread. Scheduler guard arbitrates wakeups. The syscall owns its copies, mapping lease and operation reference until it stops accessing their targets. |
| Completion status and waiters | Referenced thread object retains the result after stack reclamation. Synchronization object owns its state, mutex owner or permit count. Scheduler guard couples state changes to waiter registration. |

The initial task identifies the process in parent, session and job lookups.
Process listings and memory totals count that owner once. Scheduler snapshots
also count each execution stack and wait registration. The launcher and reaper
still allocate and release the initial task and process container together.
Step 4 separates those lifetimes for userspace workers.

### Concurrent access

A descriptor lookup takes an operation reference before releasing the table
guard. Closing or replacing a descriptor reserves its slot until the old
reference has been released. A pending read can finish through its reference
even after the descriptor closes. A failed final close restores the original
entry. Spawn retains the grants selected from the parent before installing
the child's handles.

Aliases share one file position, directory cursor and deferred error. A
nonempty file transfer commits its byte count and cursor together. A seek or
second transfer that reenters the same description returns `EBUSY`. A short
transfer returns its committed byte count. The next nonempty transfer through
any alias reports a deferred backend error once. For independent append opens,
the filesystem selects EOF and writes one transfer under the same namespace
guard. A caller that loops over short writes may interleave with another writer
between calls. Pipes keep their existing wait and atomic-write rules.

A relative path sees one working directory throughout lookup. Rename resolves
both names from that same directory. A successful chdir publishes the new path
and directory pin together. Spawn pins its starting directory for executable
lookup, the optional `.elf` lookup and the child's initial working directory.
Later parent directory changes leave that pin valid.

Each blocking syscall that uses user buffers retains a mapping lease. As long
as any lease remains, release and protection changes return `EBUSY` for dynamic
regions in that address space. Registered stack and TLS mappings are kernel
owned. A dynamic region containing a running or suspended user's stack pointer
also rejects release and read-only protection, including an empty stack at the
upper boundary.

A consuming child wait claims one completed result before copying it out.
Competing waiters can take another child or wait for the claim to resolve. A
failed result copy releases the claim for retry. A synchronous foreground
launch reserves its child for the launching thread. Opening a process handle
rechecks the target and its authority before installing the grant.

The foreground process supplies console authority. One thread at a time owns
a raw input read or an interactive terminal session. A competing reader gets
`EBUSY`. Foreground transfer revokes the old lease and wakes its waiter.
Thread-specific terminal cleanup releases only that thread's lease.

## Lock order and execution context

On this single CPU, a short interrupt guard saves/restores IF and acts as a
compiler memory barrier. The scheduler can switch only at explicit safe points
or outer user return. Follow these nesting rules when adding synchronization.

1. Update process lifecycle, session membership, wait state and runnable queues
   together under the short scheduler guard. Timer and keyboard IRQs may mark
   work here. A contended wait registers and hands control to the scheduler
   atomically. Restore IF before returning to ordinary caller code.
2. Snapshot one process resource at a time (VM, CWD or handle tables). Retain
   the mapping/object before dropping its guard. When a coherent two-table
   snapshot is required, take descriptor then typed table under one short IF
   guard. Acquire object references last and drop guards in reverse order.
   Never enter a sleepable lock, driver, allocator or destructor from that guard.
3. For file I/O the order is open description, common filesystem, FAT backend,
   block device, driver. Synchronous FS/block busy scopes exclude switching.
   An operation needing a pipe or input wait must release short guards and any
   lock needed by its producer before registering. A retained reference spans
   the wait. A scheduler wake may run under a device guard only as a bounded
   leaf update that never calls back into the device.
4. Detach references during a short update, then run final releases and backend
   callbacks in task context with IF enabled, outside switch holds and I/O
   scopes. Pure bounded rights checks may inspect protected state. Heap/PMM
   internals may briefly mask IRQs for their own bookkeeping. Neither may wait.

| Context | Permitted work |
| --- | --- |
| IRQ, nested interrupt, or ordinary IF-clear update | Bounded accounting, marking cancellation, signaling pre-existing registrations. No allocation, blocking, final release or backend calls. |
| Task, IF enabled, inside switch hold or synchronous FS/block operation | Bounded construction or synchronous work. Only documented nonblocking allocator/backend paths. Restore ownership before waiting. |
| Task, IF enabled, no protected scope | Allocate, acquire sleepable locks, validate/copy user packets, run final destruction, or wait after releasing producer dependencies. |
| Outer return to user mode | Select a runnable thread or deliver pending process cancellation using the trusted return frame. Kernel continuations finish at safe boundaries. |

These rules also apply to teardown. The existing inactive-space reaper has a
narrow nonblocking pin-release path. Split off any final release that could
acquire a sleepable lock before adding siblings.

## Thread creation, completion and process exit

Creation targets the caller's process. Reserve capacity, a fresh nonzero TID,
a handle slot and mappings before publication. Validate packet input and
writable output, retain the process and prepare both stacks privately. Publish
the handle and runnable thread together. Any failure restores the previous
counts and leaves the output unchanged. A TID is informational. Only a valid
typed handle grants access. Exhaustion fails before an ID wraps.

The worker starts at a user runtime trampoline supplied in the create packet.
On i386 the kernel supplies the opaque argument in EAX, a 16-byte-aligned ESP
at the top of its own stack, and the normal sanitized user selectors and flags.
The trampoline calls the C entry with its argument, then passes its signed
return value to thread exit. The SDK owns any startup record until the worker
has copied it. Never read a kernel address or user-supplied register frame to
construct that context.

Worker exit releases only that thread's wait, I/O, VM lease and mutex ownership.
Switch away before reclaiming its stacks and TLS. Publish the join result
after reclamation. A join observes the retained result, including on repeated
joins through aliases. Closing the last user handle lets a live worker finish.
Self-join fails with EINVAL. The initial thread keeps its released startup
contract. An explicit thread exit from the initial thread fails with EINVAL.

Returning from main, calling `rum_exit` on any thread, a user fault or accepted
process cancellation initiates process-wide shutdown. The first such decision
wins. Under the scheduler guard, reject new members, mark siblings and wake
their waits. Let outstanding syscalls finish or cancel at safe boundaries.
After the last thread is inactive and its leases have drained, one finalizer
closes both tables, drops CWD/session ownership, and destroys the inactive
space. Publish process completion once, then wake process/parent waiters.
There is no per-thread terminate grant.

## Using synchronization inside the kernel

Call `sync_request(table, &packet)` with a kernel-resident packet, in task
context with IF enabled and outside switch holds and synchronous I/O. Initialize
`version`, `size`, `operation`, `type` and the inputs listed below. Zero the
remaining words. A successful create returns its typed handle in the packet.
The shared pool holds 64 events, mutexes and semaphores combined. A full
pool returns ENOMEM. A full handle table returns EMFILE and releases the
constructor's slot. Duplicate handles can grant a subset of rights within
the same table. Requests to make them inheritable fail with EACCES.

An empty poll returns ETIMEDOUT without switching. Positive finite waits use
the PIT sleep conversion (round up and add one tick for the initial phase).
Deadlines use a half-range comparison across 32-bit tick wrap. Durations above
24 hours return ERANGE, apart from the explicit infinite-wait value.
Semaphore overflow fails the whole release. Repeated sets on an already-set
event coalesce. Manual reset releases all current waiters and stays set until
reset. Auto reset grants one FIFO waiter, or stores one signal for a later wait.

Each blocked operation pins its object and links the calling thread into one
FIFO. Timeout, grant and cancellation unlink the waiter and disarm its deadline
under the same interrupt guard. The first decision stands when execution resumes.
A committed semaphore grant consumes one permit even if process cancellation
arrives before user return. A cancelled registration consumes none. Legacy
sequence-event waits retain their existing cancellation-on-resume behavior.

IRQ producers call `sync_signal_pinned(object, count)` with an event or
semaphore reference retained in task context. Disconnect the producer before
dropping that pin. Signaling performs at most one handoff per registered
waiter, even for a release of UINT32_MAX permits. Only task context can release
a mutex. Its owner pin survives closing every handle and passes directly to the
next waiter on handoff. Thread exit abandons each owned mutex before closing
tables. With no waiter, the next acquisition reports abandonment.

Run `make test-sleep` for the direct ELF and GRUB fixtures. The wait fixture
checks the pending-IRQ registration boundary, FIFO grants, object references,
mutex abandonment and final close. The process fixture orders cancellation
on either side of a grant and checks both the semaphore count and mutex abandonment.

## Wire contract

The proposed packets live in `sdk/include/rum/abi/thread.h`. Each request
starts with version 1 and its exact byte size. Validate the whole writable
packet, copy it once, reject unknown operations, and require unused, output
and reserved fields to be zero on entry. Validate the operation's type and
rights before side effects. A timeout of zero polls, 0xffffffff waits
indefinitely, and other values are relative milliseconds up to the existing
24-hour sleep limit. Round positive waits up using checked tick arithmetic.

| Thread operation | Inputs | Success output |
| --- | --- | --- |
| CREATE | `start` in a mapped user program region and opaque `argument` | New TID and non-inheritable thread handle with QUERY, WAIT and DUPLICATE |
| SELF | None | Calling TID |
| EXIT | Signed `status` | Worker never returns |
| QUERY | Thread handle with QUERY | TID, `finished` (0 or 1), status if finished |
| JOIN | Thread handle with WAIT, `timeout_ms` | Same completed result as QUERY, or ETIMEDOUT on an unsatisfied poll/deadline |

| Synchronization operation | Rules |
| --- | --- |
| CREATE | Select EVENT, MUTEX or SEMAPHORE. Return a process-local handle with QUERY, WAIT, SIGNAL and DUPLICATE. Events accept a boolean initial value and optional MANUAL flag. Mutex starts unowned. Semaphore requires a positive maximum and initial value at most that maximum. |
| WAIT | WAIT right and matching type. Consume one semaphore permit, observe a manual event, consume an auto-reset event, or acquire a mutex. Mutex acquisition also requires SIGNAL so the caller can release it. |
| SIGNAL | SIGNAL right. Set an event, release an owned mutex, or add a positive semaphore count in `value`. Reject counts exceeding `maximum - current` before changing state. |
| RESET | EVENT plus SIGNAL. Clear signaled state. |
| QUERY | QUERY right. Return event mode/value, mutex held (0 or 1), or semaphore current/maximum. Other output words remain zero. |

Mutexes are nonrecursive. Reacquiring one's own mutex fails with EBUSY.
Only its owner may release it. Owner exit abandons it. The next acquirer takes
ownership and receives the positive `RUM_WAIT_ABANDONED` result. That caller
must repair or discard the protected data before releasing the mutex.
Wait grants and timeout/cancellation compete at one scheduler decision. A grant
commits its permit or ownership once. A later cancellation follows normal owner
cleanup. FIFO order applies among eligible registered waiters.

Successful release/signal publishes prior writes. Successful acquire/wait
observes the matching publication. Thread start sees writes before creation,
and successful join sees the worker's writes through exit. SDK syscall
boundaries must carry compiler memory clobbers. Timeout, failed polls and
ordinary unsynchronized loads provide no ordering guarantee.

Thread and synchronization objects reserve type values 8–11. Grants only
shrink on duplication. The first interface confines them to one process,
including during spawn. Calls 0–39, the initial ELF frame, descriptors 0–2,
process waits and the released 80-byte resource packet retain their ABI.
The v1 aggregate counts include synchronization objects. Its eight existing
type bins keep their original meanings.
A separate 40-byte `rum_thread_resources` snapshot reports the caller's live
and blocked threads and distinct local handles' objects by the four new kinds.
Reserve a separate dispatch operation for that packet when implementing it.
Advertise SDK features only once their calls are available.
