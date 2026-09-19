# Kernel diagnostics

Use the `diag` shell command when rum boots but something about memory or task
state looks wrong. It takes a consistent, allocation-free snapshot and prints
it to both VGA and COM1.

```text
> diag
Task: 1 | CR3: 0x00131000 | TSS.ESP0: 0x00211000
Kernel ESP: 0x00210400 | CR0: 0x8001001f
...
```

Addresses and counts vary with the build, RAM size, and current task.

## Reading `diag`

| Output | Meaning |
| --- | --- |
| `Task` | Scheduler ID of the running context; boot is 1 and idle is 2 |
| `CR3` | Page-directory address actually loaded in the CPU |
| `TSS.ESP0` | Top of the ring-0 entry stack for the current context |
| `Kernel ESP` | Stack pointer sampled while the snapshot was taken |
| `Current stack` | Valid lower and upper bounds for the running kernel stack |
| `record CR3` | Directory owned or borrowed by the current task record |
| `Paging` | Directory, shared kernel-table, and private user-table counts |
| `User pages` | Private user data pages across registered address spaces |
| `Physical` | Free pages compared with pages managed by the allocator |
| `Tasks` | Live records and their owned kernel-stack/directory pages |
| `Processes` | Published user-process records and their private user resources |
| `Lifecycle` | Created, exited, reaped, and actual context-switch totals |
| `Heap` | Live payload, mapped capacity, reusable space, and allocation count |
| `RAM files` | Current file count and total payload bytes |

Each task entry also shows whether its stack and directory are owned or
borrowed. Kernel tasks have PID zero. A published process has a positive PID,
a parent task, an owned private directory, and accounted user pages. An exited
process also shows either its hexadecimal exit status or its fault vector,
error code, and user EIP.

## What healthy output looks like

During an ordinary shell session:

- Hardware `CR3`, active paging `CR3`, and the current record's `CR3` agree.
- `Kernel ESP` lies between the current stack bounds.
- `TSS.ESP0` equals the upper stack bound.
- Exactly one task is `running`; boot and idle remain present.
- The normal shell has zero published processes and zero task-owned user pages.
- Free physical pages may decrease when the heap grows, even after individual
  heap allocations are freed. Heap pages stay mapped for reuse.

An exited process remains visible until the kernel observes its signed status
or fault record and explicitly reaps it. Its directory, user pages, and guarded
kernel stack remain owned during that interval. After reaping, the record and
all of those counts should disappear together.

Do not add paging and task directory totals together. A task-owned directory is
already included in the paging total. Paging may also contain an unpublished
space that a loader is still constructing.

## Panic output

A fatal kernel exception prints the saved CPU state first, followed by the
current task, CR3, TSS.ESP0, and kernel-stack bounds. Page faults also include
CR2 and a plain-language access description. A user exception does not use this
panic report; it becomes the process fault record described above.

COM1 receives two machine-readable lines:

- `rum_diag_cpu` contains the task kind, PID, parent, status, hardware and record
  CR3 values, current kernel ESP, TSS.ESP0, and stack bounds.
- `rum_diag_resources` contains task/process lifecycle and memory ownership
  counts, including emergency-stack, paging, heap, and RAM-file resources.

The exception's `esp` value is the interrupted stack pointer. `diag_kesp` is the
stack pointer used while producing diagnostics. They describe different moments
and are not expected to match.

For a double fault, diagnostics run from the dedicated emergency stack and the
kernel page directory. The report reconstructs the failed context from the
normal TSS before halting.

## Investigating a problem

If rum panics:

1. Copy the complete COM1 output from the launching terminal.
2. Resolve the reported EIP against the matching ELF:

   ```sh
   .tools/cross/bin/i686-elf-addr2line -e build/rum.elf -f 0xADDRESS
   ```

3. Compare hardware CR3 with `diag_recordcr3` and `diag_activecr3`.
4. Check that `diag_kesp` is within `diag_base..diag_top`.
5. Look for resource totals that fail to return to their earlier values after
   repeated creation and cleanup.

The full QEMU suite stores serial logs, screenshots, register dumps, and memory
ownership reports in `build/test-artifacts/`. Run `make test` to regenerate them.

## Diagnostics API

`diagnostics_capture` writes a `struct kernel_diagnostics` supplied by the
caller. It does not allocate, block, switch tasks, or reclaim resources, and it
restores the caller's interrupt state. Use it from foreground code or a fatal
exception. Device IRQ handlers should use the smaller, bounded
`task_snapshot_read` interface instead.

`diagnostics_render` formats an existing snapshot through a writer callback.
`diagnostics_print` captures and writes a normal report, while
`diagnostics_panic` emits the compact fatal report used by exception handling.
