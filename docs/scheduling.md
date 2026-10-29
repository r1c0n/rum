# Scheduling safety

rum preempts user processes on one CPU. Each dispatch gets five PIT ticks,
nominally 50 ms at 100 Hz. Kernel execution remains cooperative: a tick during
a syscall or kernel worker records pending work and resumes that kernel code.
Only an outer return to userspace or an explicit safe scheduling point can
select another task.

## Resource owners

Each user process has one initial thread. The private process and thread records
occupy separate bounded pools with a one-to-one relationship for now.

| State | Owner | Lifetime |
| --- | --- | --- |
| PID, parent, address space, private tables and user pages | Process resource record | Publication through reaping |
| Working directory and filesystem references | Process resource record | Cloned before publication, released by reaping |
| Handle table and backend pins | Process resource record | Initialized before publication, closed before exited state |
| Termination result, cancellation and shell session state | Process resource record | Publication through reaping |
| Kernel stack, saved kernel context and user CPU frame | Initial thread | Prepared privately, released after switching away |
| Runnable/blocked/exited state and event registration | Initial thread | Scheduler publication through reaping |
| Switch-hold nesting count | Initial thread | Balanced within one nonblocking kernel operation |
| Remaining slice and pending reschedule request | Initial thread | Reset when selected; consumed by PIT ticks |

Kernel workers use a process resource container with PID zero and may borrow
the kernel address space or own a private one. Idle borrows kernel space and
has no directory reference. The boot thread borrows the linker-provided stack.
The emergency double-fault stack is reserved independently of both pools.

The public task snapshot remains a combined view. Stack pages come from the
thread; directory, table and user pages come from its process owner. The split
introduces no new syscall or executable ABI, and no user-thread creation API.

## Where switching is allowed

| Path | Rule |
| --- | --- |
| Explicit yield or event wait | Task context, IF enabled on entry, no switch holds or active synchronous I/O |
| Exit or user fault | Copy trusted results, close handles with IRQs enabled, then mark exited and switch |
| Idle | Check runnable work with IF clear; schedule or use the existing STI/HLT sequence |
| IRQ handler | Account a tick, signal relevant events or mark cancellation; never allocate, block, reap or switch |
| Outer interrupt/syscall return to user mode | With IF clear, check cancellation and service an expired slice after protected work and temporary ownership end |
| Nested interrupt or return to kernel mode | Resume the interrupted kernel continuation; leave any scheduling request pending |
| Construction | Hold switching while preparing resources; publish a complete owner/thread pair with IF clear |
| Common filesystem or block callback | Finish synchronously before any yield, wait, exit or user return |
| Inactive-resource reaper | Release only exited, inactive owners; never switch or wait within the protected update |

The central scheduler requires IF clear, no active IRQ handler, no switch holds,
and no common-filesystem or block operation. It also checks that the current
process address space matches the active paging space and that TSS.ESP0 matches
the executing thread's stack. A violated internal invariant halts the kernel.
Public yield/wait calls refuse unsafe contexts without changing run state or
attaching an event waiter.

`task_switch_hold()` starts a nestable scope and returns true on success.
Release each successful hold once with `task_switch_release()`. Both preserve
the caller's interrupt flag and reject IRQ, idle, or uninitialized callers;
release also rejects an unmatched call. A hold is not a sleepable mutex and
does not disable interrupts. Signals may still make other threads runnable.
Yield/wait remain refused until the final hold is released.

## Time slices and interrupt return

The PIT handler calls `task_timer_tick()`. It decrements the current thread's
saturating five-tick budget and marks a request when that budget reaches zero.
It does not select a task. Further ticks leave the request pending. The budget
uses no absolute timestamp, so the 32-bit PIT clock wrapping cannot alter it.
This is a nominal dispatch budget rather than measured CPU time: kernel work
and interrupt latency can consume or delay a slice.

The common assembly path disables interrupts after C dispatch, reloads the
original frame argument from its preserved register, then calls
`task_on_user_return()`. This occurs after IRQ nesting accounting and PIC EOI.
The boundary accepts a user return only from a running process's trusted
TSS-entry frame at the top of its guarded kernel stack. A nested interrupt has
a kernel CS and resumes without scheduling, even if the slice has expired.
The check does not depend on a global interrupt depth that would become stale
when a blocking syscall switches to another thread.

The boundary records all general registers, segment selectors, EIP, EFLAGS,
user ESP and SS. On an expired slice, the ordinary scheduler saves a kernel
continuation beneath that frame. With IF clear it selects the next runnable
task in circular slot order, updates CR3, TSS and current identity, and switches
stacks. Resuming the old continuation returns to the shared assembly restore
and `iret`; its full user frame stays on its own stack throughout. Cancellation
is checked both before selection and after resuming, since it can arrive while
the process is waiting for its next turn. The existing integer-only CPU policy
remains in effect; x87, MMX and SSE instructions are still rejected.

All runnable tasks participate in the same round-robin scan. Blocking, exiting
or yielding gives up the current turn; the next selected task gets a fresh
budget. If only the current task can run, it receives another slice without a
stack switch. Idle is selected only when there is no other runnable work.
Kernel workers must still yield, wait or exit explicitly. No switch is inserted
into a switch-hold release, memory allocator, filesystem callback or driver.

Timer events remain limited to the UI work event used by uptime, recovery and
the kernel Snake loop. Keyboard reads, child completion and other event waiters
use separate events. Expiring a slice does not signal or wake those waiters.
Synchronous filesystem work and bounded ATA polling can delay scheduling until
the operation finishes and execution reaches a safe boundary. There is no
real-time latency guarantee.

## Shared state and protected updates

The task registry, process/thread publication, run states, event registrations,
foreground identity, lifecycle counters, and context-switch bookkeeping use
short interrupt-disabled updates. IRQ-side snapshots copy bounded records and
do not follow filesystem objects or allocate memory.

Construction takes a thread-owned switch hold before cloning the working
directory and allocating the guarded stack. Failure destroys only the clone
and newly allocated stack pages, releases the hold, and leaves the producer's
address space intact. Success prepares the kernel context before publication.
Neither path exposes a partially initialized runnable record.

The common filesystem serializes namespace operations with its busy flag.
`fs_operation_active()` observes that state without performing I/O. Block
wrappers retain the per-device busy flag and track active requests across all
devices. `block_request_active()` stays true until every nested request ends,
including requests that finish with a timeout or device error. The scheduler
checks both observations, so even a backend that calls yield cannot suspend
while those operations own shared state.

The current ATA PIO driver uses bounded polling and does not sleep or enable
IDE interrupts. Its wait limits and errors remain unchanged. A synchronous
transfer may delay scheduling; no responsiveness or real-time bound is implied.
Future sleeping drivers must release protected ownership before waiting, and
define request completion and cancellation lifetimes before adding a wait.

The kernel heap and paging operations do not yield. Physical allocation and
shared mapping updates retain their interrupt-protected bookkeeping. Adding
any scheduler call inside these operations requires a new ownership review.
Filesystem retain/release callbacks used by reaping must remain nonblocking
and perform no disk I/O.

## Exit, cancellation and waiting

An IRQ cancellation request marks process state and wakes a blocked initial
thread. Termination occurs at an established safe boundary. Returning to user
mode with a switch hold or active synchronous I/O is an invariant violation.
An in-progress disk write can complete before cancellation takes effect.

Exit closes handles before publishing the exited state. A surviving context
later releases the directory pin, inactive address space, and guarded stack,
then clears both pool slots. Neither the active CR3 nor the executing stack
can be reclaimed. Kernel faults retain the existing panic behavior.

Event users capture the sequence before checking their predicate, then attach
the waiter with IF clear only if that sequence is unchanged. An early signal
therefore avoids sleeping. A resumed waiter must recheck its predicate because
another waiter may consume the resource first.

## Validation

Run `make test` after changing these contracts. The task fixture in
`tests/task-kernel.c` runs through the smoke suite at 16 and 64 MiB and covers:

- Nested switch holds, rejected unmatched releases, and IRQ-context rejection.
- Attempts to yield and wait inside filesystem and block callbacks.
- Nested device requests, successful I/O, injected I/O errors and timeouts.
- Refused waits leaving the task registry and event registrations unchanged.
- Register, stack, CR3 and TSS preservation across real context switches.
- Events before wait, sequence wraparound, broadcast and real PIT wakeups.
- Insufficient construction budgets, process faults and resource reclamation.

The process and file-syscall suites additionally exercise cross-page user
validation, cancellation during input and storage work, nested foreground
children, and repeated exit/fault cleanup.

For a focused preemption check, run `make test-preemption`. It boots the fixture
through direct ELF and GRUB ISO at 16 and 64 MiB. Two CPU-bound ring-3 programs
never yield during their progress loops; a kernel monitor checks that each
advances during every round and receives a full slice. It checks private
registers, segments, flags including DF and CF, separate physical pages,
CR3/TSS agreement, absent stack guards and supervisor mappings. Repeated cases
cover cancellation while suspended, user instruction/mapping faults, a slice
expiring during a synchronous test syscall, nested timer interrupts, unrelated
waiters and resource counts returning to baseline. A host test seeds the actual
PIT counter at wraparound and verifies that every tick still reaches scheduler
accounting. Serial results are saved in `build/test-artifacts/preemption/`.
