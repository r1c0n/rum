# Kernel tasks

rum uses cooperative kernel contexts. The boot context runs the shell and
Snake; an idle context sleeps on its own stack when no task is runnable.
The 16 worker records can describe a kernel task or a prepared user process.
Timer IRQs wake tasks but do not preempt them. A worker must yield or wait to
let another context execute.

## Contexts and stacks

`task_initialize` registers the borrowed boot stack and allocates the idle and
double-fault stacks after paging is ready. Idle and every worker use a reserved
virtual slot. The first page is unmapped; four independently allocated, zeroed
pages above it form a 16 KiB supervisor stack. A failed partial allocation
removes every mapping and returns every acquired frame before the slot can be
published.

The final reserved slot belongs to double-fault recovery. IDT vector 8 uses a
hardware task gate whose TSS selects the kernel CR3 and this independent guarded
stack. Exhausting a normal kernel stack therefore reaches a controlled panic
instead of trying to report the fault through the already exhausted stack.

`kernel_context_switch` preserves the i386 C callee-saved registers, stack
pointer and return address. New contexts enter C with the expected stack
alignment and DF clear. A resumed yield/wait restores its saved interrupt
flags; a new entry starts with interrupts enabled. Interrupt frames and user
CPU-state restoration remain separate from this kernel context.

Before switching stacks, the scheduler changes CR3, active-space bookkeeping,
current-task identity, TSS.ESP0 and the normal TSS CR3 with interrupts disabled.
All directories share the supervisor kernel mappings, keeping both stacks
mapped throughout the switch. This relies on rum's single-CPU target.

## API and ownership

The interfaces are in `include/rum/task.h`:

| Interface | Behavior |
| --- | --- |
| `task_create(entry, argument, space)` | Publish a runnable worker; zero means failure |
| `task_create_process(process)` | Validate and atomically publish a prepared process record |
| `task_current_id()` | Return the running task's ID; zero before initialization |
| `task_query(id, information)` | Inspect kind, PID/parent, state, user frame, exit status and owners |
| `task_yield()` | Cooperatively select the next runnable context |
| `task_wait(event, sequence)` | Block only if no signal arrived since the snapshot |
| `task_event_signal(event)` | Advance the sequence and wake every attached waiter |
| `task_exit()` | Stop the worker and switch to another context |
| `task_exit_with_status(status)` | Record a signed result, stop, and switch away |
| `task_reap()` | Release exited workers from a surviving context |

Task IDs 1 and 2 identify boot and idle. Worker task IDs increase monotonically
and are never recycled. User processes also receive a separate positive PID and
record the creating task as their parent. Kernel tasks keep PID zero. Records
occupy a fixed registry and become invalid after reclamation.

Passing NULL to `task_create` borrows the kernel directory. Passing a registered,
inactive private directory transfers its ownership only after successful
creation. A directory cannot belong to two records. On failure, the caller keeps
its directory and argument. The argument is always borrowed; its owner must
keep it alive until the entry finishes.

`task_create_process` accepts a completed private space and a trusted user frame.
It requires exact user selectors and EFLAGS `0x202`, a mapped program EIP, and a
16-byte-aligned mapped user ESP with writable initial-stack words. The record
copies the frame and accounts for its directory, private tables, user pages and
kernel-stack pages. Until ring-3 entry is added, a trusted kernel bootstrap is
stored with the record. The record becomes runnable only after validation and
the complete guarded kernel stack succeed.

Returning from an entry calls `task_exit`. Exit marks the task exited and
switches away before cleanup. Kernel tasks are reclaimed by a resumed context
or idle. Process records remain exited so their signed status and ownership can
be inspected; an explicit `task_reap` then releases the inactive address space
and guarded stack. Boot and idle remain permanent. Parent waiting and
cancellation arrive with the foreground process-lifetime work.

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

`task_snapshot_read` copies every occupied record, state counts, process count,
owned stack/directory/user page counts and lifecycle counters under interrupt protection.
It performs no allocation and is safe from device IRQs. Before initialization,
it returns false with a zeroed result. Reclamation keeps resource release and
record clearing in the same protected operation, so snapshots cannot show an
owner whose frames have already been freed. See [kernel diagnostics](diagnostics.md)
for the `diag` command and panic reporting.

`build/tests/task.elf` links the production scheduler, switch assembly, PMM,
paging, TSS, timer and PIC. At 16 and 64 MiB RAM it checks:

- Callee-saved registers, ESP, C alignment, DF/IF, actual CR3 and TSS.ESP0
  through repeated switches between two private address spaces.
- Idle, emergency and worker guarded-stack allocation failure, retained caller
  ownership, the 16-task limit, stale IDs, and repeated stack/directory cleanup.
- Signals before waiting, sequence wrap, blocked states, broadcast wakeups,
  real PIT delivery during idle, and repeated IRQ sleep boundaries.
- Reclamation after exiting to boot or idle, and rejection of IRQ-context
  and IF-clear operations.
- Every partial stack budget, success with an exact four-page budget, and
  unchanged ownership after failed creation.
- Process selector/EFLAGS/EIP/ESP validation, positive PID and parent records,
  immutable user-frame copies, resource accounting, signed exit-status retention,
  one-owner address spaces and explicit complete cleanup.
- Allocation-free task/resource snapshots across switches and from timer IRQs.

`build/tests/task-fault.elf` triggers a real invalid-opcode exception from an
owned worker context. QEMU checks the original register report, current task,
CR3/TSS, stack bounds, protected mappings and every claimed physical frame.

`build/tests/task-double-fault.elf` deliberately pushes through a worker's guard
page. QEMU verifies the hardware task gate, emergency CR3/stack, saved failed TSS,
supervisor-only mappings, serial/VGA report and final halted CPU state.

Normal boot checks independently audit PMM ownership of the idle pages and
verify the CPU's ESP and TSS stack against boot/idle bounds. Interactive shell,
keyboard, timer and Snake tests continue to exercise the normal foreground loop.
Logs and QEMU artifacts are in `build/test-artifacts/`.
