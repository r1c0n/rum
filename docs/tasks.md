# Kernel tasks

rum uses cooperative kernel contexts. The boot context runs the shell and
Snake; an idle context sleeps on its own stack when no task is runnable.
Up to 16 worker tasks can run C entry functions. Timer IRQs wake tasks but
do not preempt them. A worker must yield or wait to let other tasks execute.

## Contexts and stacks

`task_initialize` registers the boot stack and allocates the idle stack after
paging is ready. Each worker owns four contiguous physical pages, giving it
a zeroed 16 KiB stack in the shared supervisor identity window. Stack frames
come directly from PMM, not heap payloads. Allocation can fail when free RAM
is fragmented even if the total free count is sufficient.

These stacks have no guard pages. The reserved virtual stack slots remain
available for a later guarded implementation, introduced together with an
independent double-fault stack and entry path.

`kernel_context_switch` preserves the i386 C callee-saved registers, stack
pointer and return address. New contexts enter C with the expected stack
alignment and DF clear. A resumed yield/wait restores its saved interrupt
flags; a new entry starts with interrupts enabled. Interrupt frames and user
CPU-state restoration remain separate from this kernel context.

Before switching stacks, the scheduler changes CR3, active-space bookkeeping,
current-task identity and TSS.ESP0 with interrupts disabled. All directories
share the supervisor kernel mappings, keeping both stacks mapped throughout
the switch. This relies on rum's single-CPU target.

## API and ownership

The interfaces are in `include/rum/task.h`:

| Interface | Behavior |
| --- | --- |
| `task_create(entry, argument, space)` | Publish a runnable worker; zero means failure |
| `task_current_id()` | Return the running task's ID; zero before initialization |
| `task_query(id, information)` | Inspect state, stack bounds, saved ESP and directory |
| `task_yield()` | Cooperatively select the next runnable context |
| `task_wait(event, sequence)` | Block only if no signal arrived since the snapshot |
| `task_event_signal(event)` | Advance the sequence and wake every attached waiter |
| `task_exit()` | Stop the worker and switch to another context |
| `task_reap()` | Release exited workers from a surviving context |

IDs 1 and 2 identify boot and idle. Worker IDs increase monotonically and
are never recycled; allocation stops if the ID counter is exhausted. Records
occupy a fixed registry, and an exited record becomes invalid after reclamation.

Passing NULL as `space` borrows the kernel directory. Passing a registered,
inactive private directory transfers its ownership only after successful
creation. A directory cannot belong to two tasks. On failure, the caller keeps
its directory and argument. The argument is always borrowed; its owner must
keep it alive until the entry finishes. On success, callers must not destroy
the transferred directory independently.

Returning from an entry calls `task_exit`. Exit marks the task exited and
switches away before cleanup. A resumed context or the idle context then
releases the inactive private directory and stack pages. Kernel-directory
borrowers release only their stacks. Boot and idle remain permanent.
There is no parent wait/exit-status retention or cancellation interface yet.

## Waiting and IRQ boundaries

An event is a sequence counter and a broadcast wakeup source. Keep it alive
until its waiters have resumed. Capture its sequence before checking the
associated queue or predicate, then pass that snapshot to `task_wait`:

```c
for (;;) {
    uint32_t sequence = task_event_sequence(&event);
    if (queue_has_data()) break;
    task_wait(&event, sequence);
}
```

The wait checks the sequence and attaches the blocked task in one short
interrupt-protected operation. A signal before attachment makes the wait
return immediately; a signal afterwards makes the task runnable. Resumed
callers recheck their predicate because another task may consume the data.
Sequence comparison is modulo 32 bits, including wrap through zero.

The foreground loop snapshots the shared work event while checking timer and
keyboard state. IRQ0 and accepted keyboard characters signal that event.
If nothing needs processing, boot waits and the scheduler selects idle. Idle
checks for runnable tasks with IF clear, then uses `sti; hlt`, preserving
the interrupt shadow that closes the sleep boundary.

Creating, yielding and waiting require foreground execution with IF enabled.
Do not suspend while holding a lock or another task's resource. Signals are
IRQ-safe and perform no allocation, stack switch, or reclamation.
`irq_in_handler` lets the task API reject creation, blocking, switching and
reaping from device handlers. Invalid calls to the nonreturning exit interface
halt; only workers may exit. Event signaling never runs an entry inside an IRQ.

## Tests

`build/tests/task.elf` links the production scheduler, switch assembly, PMM,
paging, TSS, timer and PIC. At 16 and 64 MiB RAM it checks:

- Callee-saved registers, ESP, C alignment, DF/IF, actual CR3 and TSS.ESP0
  through repeated switches between two private address spaces.
- Idle-stack and worker-stack allocation failure, retained caller ownership,
  the 16-task limit, stale IDs, and repeated stack/directory cleanup.
- Signals before waiting, sequence wrap, blocked states, broadcast wakeups,
  real PIT delivery during idle, and repeated IRQ sleep boundaries.
- Reclamation after exiting to boot or idle, and rejection of IRQ-context
  and IF-clear operations.

Normal boot checks independently audit PMM ownership of the idle pages and
verify the CPU's ESP and TSS stack against boot/idle bounds. Interactive shell,
keyboard, timer and Snake tests continue to exercise the normal foreground loop.
Logs and QEMU artifacts are in `build/test-artifacts/`.
