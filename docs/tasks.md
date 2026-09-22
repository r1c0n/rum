# Kernel tasks and process records

rum has a cooperative, single-CPU scheduler. The shell runs in the boot task,
an idle task sleeps when no work is ready, and up to 16 worker records can hold
a kernel task or a prepared user process.

There is no timer preemption. A running kernel task keeps the CPU until it
yields, waits, exits, or takes an interrupt. Interrupt return resumes the same
context unless foreground code later invokes the scheduler.

## Task types

| Kind | PID | Address space | Entry behavior |
| --- | --- | --- | --- |
| Boot | 0 | Borrows kernel space | Runs kernel initialization and foreground loop |
| Idle | 0 | Borrows kernel space | Sleeps with `sti; hlt` |
| Kernel worker | 0 | Borrows kernel space or owns a private space | Calls a trusted C entry function |
| User process | Positive | Owns a private space | Restores a trusted user frame and enters ring 3 |

The ELF loader can prepare a private address space and trusted frame for this
path. The shell's `run` command publishes it as the sole foreground child,
blocks the boot task on its exit event, then reports and reaps the result.

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
state, or exposing another task's partially updated resource.

## Exit and cleanup

Returning from a kernel entry is equivalent to `task_exit()`. Call
`task_exit_with_status(status)` to retain a signed result. Exit first marks the
record and switches to a surviving context; it never frees the stack or CR3
that the CPU is still using.

An exception whose saved CS came from ring 3 follows the same switch-first rule.
The process becomes exited with `TASK_TERMINATION_FAULT`, and its vector, error
code, CR2 page-fault address, EIP, ESP, and complete user frame remain available
to its parent. An exception from ring 0 still enters the kernel panic path,
including a kernel fault that occurs while serving a process.

Exited kernel workers can be reclaimed automatically by a resumed task or
idle. Exited process records remain visible so a parent can observe their
status. A foreground parent waits with `task_wait_process`, clears the foreground
registration, and calls `task_reap_process` for that exact child. Reaping destroys
the inactive address space, unmaps and frees the guarded stack, clears the
record, and makes its slot available again. `task_reap()` remains available for
bulk cleanup. Task IDs and process IDs are not recycled.

Foreground cancellation is a request, not an asynchronous stack teardown.
`task_cancel_foreground` is IRQ-safe and wakes a process blocked in a syscall.
The request becomes `TASK_TERMINATION_CANCELLED` only at process startup, after
a blocking wait resumes, or on a trusted interrupt/syscall return to ring 3.
The parent then observes and reaps it through the same path used for normal
exit and user faults.

## Inspecting task state

`task_query(id, &information)` returns one record with its kind, state, IDs,
stack bounds, directory, latest trusted frame, termination kind, exit status or
fault record, and resource ownership.

`task_snapshot_read(&snapshot)` copies the complete fixed registry without
allocating. It is safe from IRQ context and includes state totals, process and
memory counts, and lifecycle counters. The shell's `diag` command renders this
information; see [Kernel diagnostics](diagnostics.md).

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
