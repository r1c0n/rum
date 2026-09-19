# Kernel diagnostics

Enter `diag` at the shell prompt to inspect the current kernel context and
resource usage. The command takes no arguments and writes to VGA and COM1.
It reports:

- Current task ID, hardware CR3, CR0, kernel ESP and TSS.ESP0.
- Current stack bounds and the task record's directory address.
- Registered page directories, shared kernel tables, private user tables and
  private user data pages.
- Free and managed physical pages, owned task stacks, directories and process pages.
- Worker creation, exit, reclamation and actual context-switch counters.
- Heap allocations, mapped/used bytes, and RAM file counts/sizes.
- Each occupied task slot's kind, PID/parent, state, stack, directory and ownership.

The boot shell is task 1; idle is task 2. Workers receive increasing task IDs;
user processes also receive a positive PID. Exited process records remain listed
until an explicit reap so their signed result can be observed.
The lifecycle counters exclude the permanent boot/idle contexts and wrap as
unsigned 32-bit values. The switch counter excludes yields that keep the same
context running.

## Reading ownership

Boot borrows its reserved assembly stack. Idle and workers own four physical
pages each behind guarded 16 KiB virtual kernel stacks. The independent
double-fault stack owns another four pages outside task records. A kernel task
can borrow the kernel directory or own a private directory. A process owns a
private directory plus its accounted user tables and pages.

Paging counts each directory, shared kernel table, private user table and private
user data page once. The task directory count is a subset of the paging directory
count; do not add them together. Paging can also contain inactive directories
still owned by callers. Physical free/managed counts cover all allocatable
frames, including heap pages and paging structures. Reserved kernel/boot memory
is outside that managed set.

Process snapshots retain the parent task, trusted user frame and signed exit
status. Task-owned user-table and user-page totals are subsets of the paging
totals; they cover published records while paging totals can also include a
space still being built by a caller.

Freed heap blocks remain mapped for reuse, so mapped heap bytes can stay above
live payload bytes after cleanup. This is different from unreclaimed task stack
or directory frames, which should return to the physical allocator.

## Panic reports

Fatal exceptions retain their saved register and fault-address reports.
Two extra VGA lines identify the task, hardware CR3, TSS.ESP0 and kernel stack
bounds. Serial also receives `rum_diag_cpu` and `rum_diag_resources` records.
The CPU record includes task kind, PID, parent and exit status. The resource
record includes process, emergency-stack, task user-table and task user-page counts.
Their `diag_` field names distinguish current kernel state from interrupted
registers. In particular, `diag_kesp` samples the panic handler's stack;
the existing `esp` field is the interrupted stack pointer. `diag_usertables`
and `diag_userpages` retain private paging ownership counts.

Diagnostics show both hardware CR3 and registered/task-record directory
addresses rather than assuming they agree. Before subsystem initialization,
ownership counts and task ID are zero. Hardware registers and TSS remain
available, including when an isolated fault fixture uses its own page tables.
Reporting never releases resources or changes the active address space.

## Implementation and tests

`diagnostics_capture` gathers a copied snapshot under interrupt protection,
restores the caller's IF state, then lets reporting run separately. Capture
uses stack storage without heap allocation, blocking, switching or cleanup.
Call it from foreground code or a fatal exception. It walks heap metadata;
device handlers should use the bounded IRQ-safe `task_snapshot_read` instead.
`paging_stats` also returns an allocation-free, interrupt-protected snapshot.

QEMU task fixtures verify snapshots against actual CR3, TSS, guarded stack
bounds and PMM ownership through context switches and IRQ delivery. Allocation
fixtures repeat zero-, one-, two- and three-page budgets, then run a worker with
exactly four independently allocated pages. Process cases also validate the
trusted frame, PID/parent, immutable publication, signed exit status and complete
address-space cleanup. Failed setup retains caller ownership; completed teardown
restores the frame baseline.

`build/tests/task-fault.elf` faults on an owned worker stack under a private CR3.
Monitor checks compare the report with hardware, pre-fault expectations and
actual PMM bits. They reconstruct directory, shared-table, heap and stack
ownership, check supervisor permissions and kernel section protection, and
verify that panic reporting preserves every frame. These tests run at 16 and
64 MiB. Serial logs, ownership JSON, memory dumps and screenshots are retained
in `build/test-artifacts/`.
