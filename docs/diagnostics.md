# Kernel diagnostics

Enter `diag` at the shell prompt to inspect the current kernel context and
resource usage. The command takes no arguments and writes to VGA and COM1.
It reports:

- Current task ID, hardware CR3, CR0, kernel ESP and TSS.ESP0.
- Current stack bounds and the task record's directory address.
- Registered page directories and shared kernel page tables.
- Free and managed physical pages, owned task stacks and directories.
- Worker creation, exit, reclamation and actual context-switch counters.
- Heap allocations, mapped/used bytes, and RAM file counts/sizes.
- Each occupied task slot's state, stack bounds, directory and ownership.

The boot shell is task 1; idle is task 2. Workers receive increasing IDs.
Exited records remain listed until a surviving context reclaims them.
The lifecycle counters exclude the permanent boot/idle contexts and wrap as
unsigned 32-bit values. The switch counter excludes yields that keep the same
context running.

## Reading ownership

Boot borrows its reserved assembly stack. Idle and workers own four physical
pages each for their 16 KiB kernel stacks. A worker can borrow the kernel
directory or own a private directory transferred at successful creation.

Paging counts each directory once and each shared kernel table once. The task
directory count is a subset of the paging directory count; do not add them
together. Paging can also contain inactive directories still owned by callers.
Physical free/managed counts cover all allocatable frames, including heap pages
and paging structures. Reserved kernel/boot memory is outside that managed set.

Freed heap blocks remain mapped for reuse, so mapped heap bytes can stay above
live payload bytes after cleanup. This is different from unreclaimed task stack
or directory frames, which should return to the physical allocator.

## Panic reports

Fatal exceptions retain their saved register and fault-address reports.
Two extra VGA lines identify the task, hardware CR3, TSS.ESP0 and kernel stack
bounds. Serial also receives `rum_diag_cpu` and `rum_diag_resources` records.
Their `diag_` field names distinguish current kernel state from interrupted
registers. In particular, `diag_kesp` samples the panic handler's stack;
the existing `esp` field is the interrupted stack pointer.

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

QEMU task fixtures verify snapshots against actual CR3, TSS, stack bounds and
PMM ownership through context switches and IRQ delivery. Allocation fixtures
repeat zero-, one-, two- and three-page budgets, reject four scattered free
pages, then run a worker with exactly four contiguous free pages. Failed setup
retains caller ownership; completed teardown restores the frame baseline.

`build/tests/task-fault.elf` faults on an owned worker stack under a private CR3.
Monitor checks compare the report with hardware, pre-fault expectations and
actual PMM bits. They reconstruct directory, shared-table, heap and stack
ownership, check supervisor permissions and kernel section protection, and
verify that panic reporting preserves every frame. These tests run at 16 and
64 MiB. Serial logs, ownership JSON, memory dumps and screenshots are retained
in `build/test-artifacts/`.
