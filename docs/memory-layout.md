# Memory layout and ownership

`include/rum/memory_layout.h` defines the address ranges used by C, boot
assembly, and the linker. `include/rum/process_limits.h` sets the initial
process resource policy. End addresses in this guide are exclusive.

## Address ranges

| Start | End | Purpose |
| --- | --- | --- |
| `0x00000000` | `0x40000000` | Kernel identity window, capped at 1 GiB |
| `0x40000000` | `0x40400000` | Kernel heap, 4 MiB |
| `0x40400000` | `0x7fc00000` | General kernel aliases |
| `0x7fc00000` | `0x80000000` | Reserved kernel stacks |
| `0x80000000` | `0xbfc00000` | Reserved user programs and data |
| `0xbfc00000` | `0xc0000000` | Reserved user-stack window |
| `0xc0000000` | 4 GiB | Unassigned |

The kernel still loads at `0x00200000`. Its 16 KiB boot stack stays in BSS
and is covered by the kernel's permanent physical reservation. The first MiB
is also reserved; page zero remains unmapped. The identity mapping ends at
the detected RAM limit, which may be below the window's cap.

The current mapping API accepts only the heap and general kernel alias
ranges. Heap callers own the heap range; other callers start at
`RUM_KERNEL_ALIAS_BASE`. Reserved stacks, user addresses, and unassigned
addresses are rejected before any page table is allocated. Boundary constants
are aligned to whole page-table spans to keep future shared kernel tables
separate from private user tables.

Reserving a range does not allocate or map it. Separate directories now borrow
shared kernel tables; processes, private user mappings, and dedicated stack
mapping APIs are subsequent work. See [Address spaces](memory.md#address-spaces).

## Stack and process limits

| Resource | Initial limit |
| --- | --- |
| User processes | 16, excluding kernel contexts |
| Private user memory per process | 16 MiB of mapped pages, including the stack |
| Kernel stack per context | 16 KiB |
| Kernel stack slots | 18: processes, idle, and emergency context |
| User stack per process | 64 KiB |
| Arguments | 32, including `argv[0]` |
| Argument strings | 4096 bytes total, including terminating NULs |
| Open file handles per process | 32, including standard streams |

These are policy constants for the forthcoming process implementation. The
corresponding runtime checks must be added with each subsystem. Allocation
can fail below any limit when physical RAM or kernel metadata is exhausted.

Kernel stack slot `n` starts at
`RUM_KERNEL_STACK_BASE + n * RUM_KERNEL_STACK_STRIDE`. Its first page is
reserved as a guard, followed by the 16 KiB stack. Slot addresses must be
unique across live kernel contexts because kernel mappings will be shared.
Install guards together with the independent double-fault recovery path.

Each process uses a private user stack at `0xbfff0000`–`0xc0000000`, growing
downward from `RUM_USER_STACK_TOP`. The page at `0xbffef000` is its guard.
Other pages in the user-stack window stay reserved and unmapped. These
virtual addresses can be reused across processes with separate directories.

## Ownership

The physical allocator tracks page availability. The component allocating a
page owns it until ownership is explicitly transferred. Mapping a data page
adds an alias; it does not transfer ownership. Unmapping removes the alias
without freeing the data page.

| Resource | Owner and release rule |
| --- | --- |
| Kernel image and boot metadata | Permanently reserved; never returned to PMM |
| Kernel directory and identity tables | Kernel paging; initialization failure rolls back, successful boot retains them |
| Dynamic kernel tables | Kernel paging; publish in every directory, detach everywhere before reclaiming an empty table |
| Heap frames | Kernel heap; kept mapped for reuse after individual allocations are freed |
| Kernel metadata allocations | Allocating subsystem; release with `kfree` after references are removed |
| Paging-context directory and metadata | Address space; release only while inactive; borrowed kernel tables remain owned by the kernel |
| Future private user tables | Address space; release while inactive after private mappings are removed |
| Future private program and user-stack frames | Address space; allocate directly from PMM and release after removing all mappings |
| Shared kernel table references | Kernel paging; context destruction never frees the referenced tables or kernel data frames |
| Future kernel stack frames and slot | Kernel context; release only after switching to a surviving stack |
| Future process record and argument copies | Process manager; release after handles, execution state, and memory have been detached |
| Future file handles | Process handle table; close on exit and failed process construction |

User memory is page-backed rather than a heap payload. Clear each private
frame before exposing it to userspace, including ELF padding and unused
stack bytes. Record ownership once per physical frame even if it has multiple
aliases. A writable identity alias does not make a read-only kernel alias
safe from other kernel code.

## Construction and cleanup

Build a process privately before publishing it as runnable. Check limits and
reserve a process/stack slot first, then allocate metadata, its directory,
private tables, program pages, stack pages, and handles as needed. Register
each successful acquisition in the owner's record immediately. An operation
that fails must leave existing mappings and owners unchanged.

On failure, unwind only acquired resources in reverse dependency order:
close handles, remove private mappings, free owned data frames and private
tables, release the inactive directory and stack, then free metadata and
return slots. Borrowed shared kernel tables are never part of this rollback.
Use the same release rules for normal exit and faults. Keep quota accounting
with the owning record so a partial load cannot leave a stale charge.

Never free the active CR3, the currently executing stack, or a structure still
referenced by another context. Exit and fault paths must first switch to a
surviving kernel context; reclamation happens afterwards. Interrupt handlers
may queue events or request cancellation, but must not allocate, block, or
destroy these resources.

## Verification

Host layout tests check exclusive bounds, overflow, and separation of stack
reservations from general mappings. QEMU paging tests exercise the production
API at the last permitted page and reserved ranges, checking translation,
table accounting, caller frame ownership, and rollback. Every boot fixture
also checks the linked kernel and boot-stack addresses and sizes.

Paging-context tests cover shared heap growth, new kernel tables, active CR3
bookkeeping, interrupt return under child directories, lifecycle limits, and
allocation-failure cleanup. They distinguish retained heap pages from leaked
directory frames and live metadata.

Before these changes, commit `6985f698` passed the seven C host tests, embedded
file generator tests, and all 19 QEMU cases. The 64 MiB GRUB boot reported
16,255 usable pages, 16,035 managed pages, and 16,016 free pages; paging used
18 frames. The heap mapped one page with 880 live payload bytes and two files.
Memory reports for subsequent runs are in `build/test-artifacts/*-memory.json`.
