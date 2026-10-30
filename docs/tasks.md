# Kernel tasks and process records

rum has a single-CPU scheduler with timer-driven user-process preemption. The boot task supervises the
userspace shell, an uptime worker updates the status row, and an idle task sleeps
when no work is ready. Up to 16 worker records can hold a kernel task or a user
process. The recovery shell runs in the boot task.

User processes receive five PIT ticks per dispatch, nominally 50 ms. A running
kernel task keeps the CPU until it yields, waits or exits. Interrupts during
kernel execution leave scheduling requests pending; an outer return to user
mode can service them once protected work has finished. Bounded synchronous
I/O can therefore delay scheduling.

## Task types

The scheduler's private records separate process resources from thread execution.
`process_state` owns the PID, parent relationship, address space, working
directory, handles, termination result, cancellation request, and shell-session
state. The schedulable `task` is its initial thread: it owns the kernel stack,
saved context, user frame, run state, wait registration, and switch holds.

There is one initial thread per process. Kernel workers use the same resource
container with PID zero; idle has no working directory. The two bounded pools
are published together with interrupts disabled, and cleared together only
after execution has switched away. Public task queries still combine both
records, so diagnostics and existing callers keep their interface.

See [Scheduling safety](scheduling.md) for protected operations and the rules
for adding a scheduling point.

| Kind | PID | Address space | Entry behavior |
| --- | --- | --- | --- |
| Boot | 0 | Borrows kernel space | Initializes the kernel, supervises the shell, runs recovery |
| Idle | 0 | Borrows kernel space | Sleeps with `sti; hlt` |
| Kernel worker | 0 | Borrows kernel space or owns a private space | Calls a trusted C entry function |
| User process | Positive | Owns a private space | Restores a trusted user frame and enters ring 3 |

The ELF loader can prepare a private address space and trusted frame for this
path. The shell's `run` command publishes a foreground child through a syscall,
blocks the shell on its exit event, then reports and reaps the result. Nested
launches transfer foreground ownership to the deepest child and restore its
parent after termination. See [process syscalls](process-syscalls.md).

## Guarded kernel stacks

Idle and each worker use a slot in `0x7fc00000`–`0x80000000`. Every slot is:

```text
unmapped 4 KiB guard
mapped 16 KiB supervisor stack
```

The four stack pages are allocated independently, cleared, and shared into all
registered page directories as supervisor-only mappings. A partial allocation
is rolled back before the task record becomes visible.

The boot task borrows its linker-provided BSS stack. The final virtual slot is
reserved for double-fault recovery and never belongs to a schedulable task. See
[Double faults](exceptions.md#double-faults).

## Scheduler state changes

A context switch runs with interrupts disabled and updates these values as one
operation:

1. Previous and next task states.
2. Active page-space bookkeeping and hardware CR3.
3. Normal TSS ESP0 and CR3.
4. Current task identity and stack-top diagnostics.
5. The saved kernel stack pointer through `kernel_context_switch`.

All spaces share kernel code, data, heap, and guarded stack mappings, so both
the old and new stacks remain addressable during the switch.

`kernel_context_switch` preserves the i386 C callee-saved registers. New task
entries begin with DF clear, correct C stack alignment, and interrupts enabled.
Timer preemption suspends a kernel continuation beneath the complete user
interrupt frame. Resuming it restores every saved user register and EFLAGS
through the common `iret` path. See [Scheduling safety](scheduling.md#time-slices-and-interrupt-return)
for return-frame validation, fairness and cancellation rules.

## Creating a kernel task

```c
static void worker(void *argument)
{
    /* Do bounded foreground work. */
    task_yield();
    /* Returning exits with status zero. */
}

task_id id = task_create(worker, argument, NULL);
```

Passing `NULL` borrows the kernel address space. Passing a registered inactive
private space transfers it only when creation succeeds. The same private space
cannot belong to two live records. The `argument` pointer is borrowed and must
remain valid until the entry finishes.

Creation requires foreground context with IF set. A zero task ID means the
entry, space, interrupt state, registry capacity, or stack allocation was
invalid. On failure, the caller still owns the supplied space and argument.

## Publishing a process record

`task_create_process` accepts a `struct task_process` containing:

- A completed, inactive private address space.
- A trusted `exception_user_frame`.

The frame must use the exact user selectors and EFLAGS `0x202`. Its EIP must be
mapped in the user program range, and its 16-byte-aligned ESP must cover writable
initial-stack words. The function copies the frame and records the directory,
private-table, user-page, and kernel-stack ownership.

The record becomes runnable only after validation and complete stack setup. A
failed call publishes nothing and leaves the prepared address space with the
caller. A successful record receives a monotonically increasing task ID, a
positive process ID, and the current task as its parent.

When the scheduler first selects the process, it switches CR3, updates TSS.ESP0,
and runs `interrupt_enter`. That assembly helper replaces the bootstrap kernel
stack with the trusted frame and shares the same `iret` restore path used by
returning interrupts. EFLAGS enables interrupts as the CPU enters ring 3.

## Yielding, waiting, and waking

Use `task_yield` when another runnable context should get a turn. Use an event
when a task should sleep until state changes:

```c
for (;;) {
    uint32_t observed = task_event_sequence(&event);
    if (queue_has_data()) break;
    if (!task_wait(&event, observed)) {
        /* Invalid context or interrupt state. */
    }
}
```

Capture the event sequence before checking the predicate. `task_wait` compares
the same sequence while attaching the waiter with interrupts disabled. A signal
between the check and wait therefore makes the call return without sleeping.

`task_event_signal` is IRQ-safe. It advances the sequence and makes every task
waiting on that event runnable. Woken tasks must recheck their predicate because
another task can consume the resource first.

The keyboard owns a separate input event in addition to the foreground work
event. A blocking standard-input syscall waits on that narrower event so PIT
ticks cannot wake it when no character has arrived.

Do not yield or wait while holding a lock, owning temporary interrupt-disabled
state, or exposing another task's partially updated resource. Use a balanced
`task_switch_hold` / `task_switch_release` scope for nonblocking construction
or other operations that must not suspend. Holds nest and belong to the current
thread. IRQ, idle, uninitialized, and unbalanced calls return false.

Yield and wait also refuse an active common-filesystem operation or block
request. These operations run synchronously with interrupts enabled, but no
backend callback may suspend while it owns namespace or device state. Rejected
waits do not attach a waiter. The central switch asserts these restrictions,
IF-clear entry, and agreement between the current owner, active CR3, and TSS.

For an event with a timeout, use
`task_wait_for(&event, observed, timeout_ticks)`. Its result distinguishes
`TASK_WAIT_EVENT`, `TASK_WAIT_TIMEOUT`, `TASK_WAIT_CANCELLED` and
`TASK_WAIT_INVALID`. A null event waits only for the timeout. The relative tick
interval must be less than `TIMER_HALF_RANGE` (2^31); zero checks the event once
and otherwise times out immediately. An already changed sequence wins over a
zero timeout. Invalid contexts or intervals attach no waiter.

Event-sequence comparison, tick capture and registration happen with IF clear.
The first event or deadline to wake a blocked task removes both registrations
before making it runnable. A cancellation pending on entry or resumption takes
priority over the wake reason. Keep the event alive until its waiters resume;
none of these helpers frees an event or terminates its caller. Kernel code must
release any temporary ownership before applying cancellation at a safe boundary.
Existing `task_wait` callers still recheck their predicate and cancellation flag
after a successful wake.

`task_sleep(milliseconds)` uses the same deadline machinery and checked duration
conversion as the public SDK call. It returns zero on timeout, `-RUM_EINTR` when
cancelled, or a range/context error. Zero milliseconds yields. The public limits
and tick rounding are described in [process syscalls](process-syscalls.md#yielding-and-sleeping).

## Exit and cleanup

Returning from a kernel entry is equivalent to `task_exit()`. Call
`task_exit_with_status(status)` to retain a signed result. Exit closes every
file/stream handle with IRQs enabled, then marks the record and switches to a
surviving context; it never frees the stack or CR3
that the CPU is still using.

An exception whose saved CS came from ring 3 follows the same switch-first rule.
The process becomes exited with `TASK_TERMINATION_FAULT`, and its vector, error
code, CR2 page-fault address, EIP, ESP, and complete user frame remain available
to trusted kernel callers. A userspace parent receives only the fault vector.
An exception from ring 0 still enters the kernel panic path,
including a kernel fault that occurs while serving a process.

Exited kernel workers can be reclaimed automatically by a resumed task or
idle. Exited process records remain visible so their parent can observe their
status. `task_wait_child` uses a public PID (zero for any child) and returns one
completed record without consuming it, zero for a pending nonblocking check,
or a public error. Each parent has a separate child-completion event. The caller
copies the result before `task_collect_child` acknowledges the completed launch
and reaps that child. Foreground ownership is restored during exit, before
completion is signalled. `task_wait_process` remains a task-ID compatibility wrapper.
Reaping destroys
the inactive address space, unmaps and frees the guarded stack, clears the
record, and makes its slot available again. `task_reap()` remains available for
bulk cleanup of the caller's completions and adopted orphans; it cannot discard
another parent's results. Task IDs and process IDs are not recycled. Launch
preflight reports `-RUM_EOVERFLOW` when either identity namespace is exhausted.

Parent exit adopts, cancels and eventually reaps every remaining descendant,
including abandoned exit records. The bounded descendant set is discovered
before parent links change, and each member is queued once. Boot owns cleanup,
and adopted process results are not exposed to public waits. No active stack
or directory is destroyed by adoption; live children finish cancellation on
their own contexts. See [process syscalls](process-syscalls.md#parent-exit-limits-and-identity-lifetime).

Foreground cancellation is a request, not an asynchronous stack teardown.
`task_cancel_foreground` is IRQ-safe and wakes a process blocked in a syscall.
The request becomes `TASK_TERMINATION_CANCELLED` only at process startup, after
a blocking wait resumes, or on a trusted interrupt/syscall return to ring 3.
The parent then observes and reaps it through the same path used for normal
exit and user faults.

`task_create_shell_process` publishes the initial shell and its interactive role
atomically, so Ctrl+C cannot cancel it between publication and role assignment.
Use `process_start_shell` to load that supervised root from a file. Ordinary
process factories cannot grant this role. Ctrl+C is queued for the shell's
line editor; its foreground children remain cancellable. The private
[console session layer](console-sessions.md#kernel-ownership) retains owners
and restores the immediate live parent, independently of task parent-ID changes.

Each process gets a fresh [32-slot handle table](filesystem-syscalls.md), with
standard streams in 0–2. Open files are not inherited. File references are
released before the exited state is visible, including fault and cancellation
paths. The working-directory reference remains until reaping. Construction
initializes streams only after stack allocation succeeds, so a partial failure
cannot publish a handle or leak a backend pin. `task_handle_count(id)` provides
an IRQ-safe count without following backend objects.

## Working directories

The boot task starts at `/`. Each worker or process inherits its creator's
working directory as an independent referenced context. Use
`task_current_filesystem()` in foreground kernel code and `fs_context_chdir`
to change it. A child's change leaves the parent unchanged. Idle owns no
working directory.

Failed task construction releases the cloned reference. Exit, user fault, and
cancellation retain it until the task is reaped; a disk mount stays busy while
an unreaped process still owns a directory on it. Reaping releases the directory
pin without performing filesystem I/O. See [Filesystems and paths](filesystems.md)
for path limits, mount boundaries, and backend lifetime rules.

`task_working_directory(id, buffer, capacity)` copies a path without allocating
and is safe from IRQ context. It returns false for idle, an unknown task, or
an insufficient buffer.

## Inspecting task state

Boot and task stacks are each 16 KiB. A full `task_snapshot` occupies several
KiB, so keep bulk comparison buffers off the stack when nested construction
or diagnostics already use it. Test-only static buffers are suitable when one
boot context owns the check; they also avoid allocations during memory-exhaustion
tests. Preserve the guarded-stack limits when adding inspection code. The task
fixture rejects individual stack frames larger than 8 KiB at compile time.

`task_query(id, &information)` returns one record with its kind, state, IDs,
stack bounds, directory, latest trusted frame, termination kind, exit status or
fault record, and resource ownership.

`task_snapshot_read(&snapshot)` copies the complete fixed registry without
allocating. It is safe from IRQ context and includes state totals, process and
memory counts, and lifecycle counters. The shell's `diag` command renders this
information; see [Kernel diagnostics](diagnostics.md).
The snapshot also counts `event_waiters` and `deadline_waiters`; a timed event
wait contributes to both. Wakeup and exit detach both registrations before
collection, and an exited record cannot be reaped with a wait still attached.

## Common mistakes

- **Task creation returns zero:** confirm IF is enabled, the entry is non-null,
  the private space is registered and inactive, and a registry slot is free.
- **A process definition is rejected:** verify selectors, EFLAGS `0x202`, EIP
  mapping, 16-byte ESP alignment, and writable stack coverage.
- **A process fault halts the whole kernel:** verify the common frame reports a
  ring-3 CS and that the current task is a process. Ring-0 faults intentionally
  retain the panic behavior.
- **A waiter never wakes:** capture the event sequence before checking the shared
  predicate and signal the same event after changing that predicate.
- **A cleanup path faults:** never destroy the active CR3 or unmap the currently
  executing stack. Switch first, reap second.
- **Memory counts grow after repeated workers:** compare task and paging totals
  before publication and after reaping with `diag`.

Run `make test` after changing scheduling, context assembly, task states, stack
mapping, process publication, event waits, or cleanup.
