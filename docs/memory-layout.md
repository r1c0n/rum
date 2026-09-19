# Memory map and ownership

This page is the quick reference for choosing an address range and deciding who
must release a memory resource. Constants live in
`include/rum/memory_layout.h`; process limits live in
`include/rum/process_limits.h`. Every end address below is exclusive.

## Virtual address map

| Start | End | Purpose | Mapping policy |
| --- | --- | --- | --- |
| `0x00000000` | `0x40000000` | Kernel identity window | Supervisor-only; page zero absent; capped at detected RAM and 1 GiB |
| `0x40000000` | `0x40400000` | Kernel heap | Shared supervisor mappings created on demand |
| `0x40400000` | `0x7fc00000` | General kernel aliases | Shared supervisor mappings owned by kernel paging |
| `0x7fc00000` | `0x80000000` | Guarded kernel stacks | Shared supervisor stack pages with unmapped guards |
| `0x80000000` | `0xbfc00000` | User programs and data | Private user mappings per address space |
| `0xbfc00000` | `0xbffef000` | Reserved stack-window gap | Unmapped |
| `0xbffef000` | `0xbfff0000` | User stack guard | Unmapped |
| `0xbfff0000` | `0xc0000000` | User stack | Private user mappings, growing downward |
| `0xc0000000` | 4 GiB | Unassigned | Unmapped |

The kernel loads at `0x00200000`. Its 16 KiB boot stack is part of BSS and stays
reserved for the kernel lifetime. The first MiB covers firmware and legacy
device memory and is never returned by the physical allocator.

Reserving a virtual range does not allocate pages. The heap, alias, stack, and
user APIs decide when to create mappings and enforce the boundaries above.

## Kernel stack slots

A virtual kernel stack slot has one guard page followed by a 16 KiB stack:

```text
slot guard = RUM_KERNEL_STACK_BASE + slot * RUM_KERNEL_STACK_STRIDE
slot base  = guard + RUM_PAGE_SIZE
slot top   = base + RUM_KERNEL_STACK_SIZE
```

Slot 0 belongs to idle. Worker record index 2 starts at slot 1. The final slot
belongs to double-fault recovery. The guard is never mapped, and each stack page
owns an independently allocated physical frame.

Stack mappings are present in every registered page directory because the CPU
must be able to switch stacks while changing CR3. They remain supervisor-only.

## Resource limits

| Resource | Limit | Enforced by |
| --- | --- | --- |
| Worker/process records | 16, plus permanent boot and idle records | Task registry |
| Private user memory | 16 MiB per address space, including user stack | Paging |
| Kernel stack | 16 KiB plus a 4 KiB guard | Stack-slot paging |
| Kernel stack slots | 18: idle, 16 workers, emergency | Layout assertions |
| User stack | 64 KiB plus a 4 KiB guard | User mapping API |
| Arguments | 32 including `argv[0]` | Public ABI; launch path pending |
| Argument strings | 4096 bytes including NULs | Public ABI; launch path pending |
| RAM files | 64 files, 64 KiB each | RAM filesystem |
| Process handles | 32 including standard streams | Reserved ABI policy; handle table pending |

Physical RAM or kernel metadata can run out before a policy limit is reached.
Callers must handle allocation failure without changing existing ownership.

## Ownership table

| Resource | Owner | Release rule |
| --- | --- | --- |
| Kernel image, boot stack, and Multiboot data | Kernel reservation | Never released |
| Kernel directory and identity tables | Kernel paging | Retained for kernel lifetime |
| Dynamic shared kernel table | Kernel paging | Remove from every space, reload active CR3, then free when empty |
| Heap data frame | Kernel heap | Stays mapped for reuse even after blocks are freed |
| Heap metadata block | Allocating subsystem | Release with `kfree` after removing references |
| Private directory and paging metadata | Address space, then task/process after transfer | Destroy only while inactive |
| Private user table and data page | Address space | Release explicitly or during inactive-space destruction |
| Guarded worker stack | Task/process record | Unmap and free only after switching away |
| Idle and double-fault stacks | Task system | Retained for kernel lifetime |
| Task/process record | Fixed registry | Clear only after every owned resource is detached |
| Shared kernel-table reference | Borrowing address space | Never frees the referenced kernel table |
| RAM-file node and payload | RAM filesystem | Release on removal or atomic replacement |

An alias does not transfer ownership. `paging_unmap_page` removes a virtual
mapping but does not free the returned data frame. The component that allocated
that frame remains responsible for it.

## Process publication and cleanup

A process address space is built while it is inactive and owned by the caller.
Program pages, initial stack contents, and the trusted CPU frame must be complete
before `task_create_process` is called.

On successful publication, ownership of the space moves to the process record.
On failure, the caller keeps the space and tears it down. The kernel stack is
the only resource acquired by publication itself, and partial stack allocation
is rolled back internally.

Exit changes state and switches to a surviving context before reclamation. The
reaper may then destroy the inactive CR3, user pages and tables, guarded kernel
stack, and record. Never free the active directory, the executing stack, or a
structure still referenced by another owner.

## Rules to preserve

- Keep shared kernel mappings supervisor-only in every directory.
- Zero a private user frame before publishing its PTE.
- Validate a whole range before copying any user byte.
- Roll back only resources acquired by the failed operation.
- Do not count borrowed kernel tables as private address-space ownership.
- Invalidate or reload translations before reusing an unmapped frame.
- Keep accounting in the owner record so `diag` can explain every live page.

See [Memory management](memory.md) for the allocator and paging APIs and
[Kernel tasks](tasks.md) for stack and process lifetimes.
