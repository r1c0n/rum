# Memory management

rum uses GRUB's Multiboot v1 memory map to allocate physical RAM and enable
32-bit, non-PAE paging. Physical allocation and the identity window are limited
to addresses below `0x40000000` (1 GiB).

Shared constants, reserved stack/user ranges, and resource ownership are
described in [Memory layout and ownership](memory-layout.md).

## Physical allocator

`include/rum/multiboot.h` describes the boot information using fixed-width
physical addresses. The parser requires information flag 6 and a nonempty map;
it does not infer free RAM from `mem_upper`. Entry sizes allow extensions to be
skipped. Truncated entries, address overflow, and invalid metadata lengths
are rejected. Boot information must reside in readable memory supplied by the
bootloader.

Only whole 4096-byte pages in type-1 RAM are available. Partial pages are
excluded, and reserved entries take precedence over overlapping available
entries. RAM above 1 GiB is ignored.

Two fixed bitmaps in kernel BSS use 64 KiB in total: one tracks managed pages,
the other tracks allocations. Initialization reserves:

- The first MiB, including firmware and legacy device memory.
- The entire kernel range, including BSS, stack, CPU tables, and allocator state.
- Multiboot information, maps, strings, modules, symbol tables, and loaded ELF
  section payloads.
- Reported drive, BIOS configuration, APM/VBE, framebuffer, and palette data.

Reservations cover every touched page, including unaligned objects. Boot
strings must terminate within 4096 bytes, and kernel pages must be described
as usable RAM before reservation. Boot resources remain reserved for the
kernel's lifetime.

| API | Behavior |
| --- | --- |
| `pmm_allocate_page()` | Return an aligned physical address, or zero on failure |
| `pmm_free_page(address)` | Free an allocated page; reject invalid, reserved, or already-free pages |
| `pmm_stats()` | Report usable, managed, and free pages and the RAM-window limit |

Allocated pages contain uninitialized data. Allocation, freeing, and statistics
snapshots briefly preserve and disable interrupts. The allocator supports a
single CPU. Page directories and tables consume allocated pages like other
kernel data. The serial marker `rum_memory_ok` records boot memory statistics.

## Paging

`arch/i386/paging.c` allocates and clears a page directory and 1024-entry page
tables. It identity maps addresses from `0x1000` to the RAM-window limit,
including gaps and reservations. This permits physical access while the
allocator prevents reserved memory from being handed out. Page zero is unmapped.

Shared kernel mappings are supervisor-only. Kernel `.text` and `.rodata` are
read-only; data, BSS, stack, and page tables are writable. Private mappings in
registered user spaces carry the user bit and may be read-only or writable.
Legacy VGA/ROM pages from `0xa0000` to `0x100000` have caching disabled.

CR3 points to the physical directory. CR4 clears PAE, large-page, and global-page
modes. CR0 enables PG and WP so ring-0 writes to read-only pages fault. This
paging mode has no NX bit. Demand paging and swap are unsupported.

Initialization failure frees allocated tables and leaves paging disabled.
Invalid boot maps or paging failures are reported before the kernel halts.

## Address spaces

Paging operations take an opaque `struct paging_space *`. The permanent kernel
space owns the identity and dynamic kernel tables. A new space owns its own
directory frame and heap metadata, and borrows the kernel's tables below
`RUM_USER_BASE` (`0x80000000`). Kernel code, CPU tables, the boot stack, and the
heap therefore remain accessible with supervisor-only permissions under every
directory. User and unassigned ranges start unmapped.

| API | Behavior |
| --- | --- |
| `paging_kernel_space()` | Return the permanent kernel space, or `NULL` before initialization |
| `paging_active_space()` | Return the space currently loaded in CR3 |
| `paging_directory_address(space)` | Return its physical directory address, or zero for an unregistered handle |
| `paging_space_create()` | Create a directory borrowing kernel tables; require an initialized heap; return `NULL` on failure |
| `paging_switch_space(space)` | Load a registered directory into CR3, preserve interrupt flags, and update active-space bookkeeping |
| `paging_space_destroy(space)` | Release an inactive directory, private user pages/tables, and metadata; retain borrowed kernel resources |

Up to 16 additional spaces can coexist with the kernel directory. Creation
checks that bound before allocation and publishes a handle only after its
directory is ready. Allocation failure releases acquired metadata. Destruction
refuses the kernel space, active space, unregistered handles, and unsupported
private directory entries. A handle is invalid after successful destruction.
Each space records its private table and data-page counts so destruction can
validate and release only resources owned by that space.

The heap always maps through the kernel owner, even when a different space is
active. Existing tables are shared directly, so added heap pages appear in
every space. Creating a new kernel table publishes its directory entry in all
registered spaces. Removing the last mapping clears that entry everywhere and
reloads the active CR3 before returning the empty table's frame to PMM.
Registry updates and mapping changes preserve and briefly disable interrupts;
creation, destruction, and mapping are foreground operations on a single CPU.

Switching reloads CR3, discarding cached translations; global-page mode remains
disabled. This also makes changes made while a directory was inactive visible
on its next activation. See the [Intel SDM, Volume 3A, section 5.10.4.1](https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf)
for translation-cache invalidation rules.

Paging contexts can contain owned anonymous user pages and transfer into an
atomically published process record. ELF loading and ring-3 entry follow in
later 0.3.0 work.

## User mapping and copy API

`paging_user_allocate` maps zeroed private pages into an inactive or active
registered user space. A request must be page-aligned and fit wholly inside the
program range (`0x80000000`–`0xbfc00000`) or the fixed 64 KiB user stack
(`0xbfff0000`–`0xc0000000`). The stack guard at `0xbffef000`, the unused gap,
page zero, and addresses at or above `0xc0000000` are rejected.

The 16 MiB per-space page budget is checked before allocation. Every data frame
is cleared before its PTE is published. If a data frame or page table cannot be
allocated, the operation removes only mappings created by that request and
returns their frames. Existing mappings and their contents remain unchanged.

| API | Behavior |
| --- | --- |
| `paging_user_allocate(space, address, pages, flags)` | Own and map zeroed anonymous pages; reject overlaps and roll back the full request on failure |
| `paging_user_protect(space, address, pages, flags)` | Change write permission only after validating every page in the range |
| `paging_user_release(space, address, pages)` | Remove and free every page, then reclaim newly empty private tables |
| `paging_user_page_count(space)` | Return the number of private data pages charged to the space |
| `paging_user_accessible(space, address, bytes, writable)` | Validate the complete byte range and requested access without copying |
| `paging_copy_from_user` / `paging_copy_to_user` | Validate all covered pages before moving any byte across the kernel boundary |
| `paging_copy_string_from_user` | Copy through the first NUL within capacity; leave outputs unchanged on failure |

Zero-length buffer operations succeed for a valid user space without inspecting
the address or buffer. Nonempty operations reject arithmetic overflow, holes,
guard pages, addresses outside the user window, and writes spanning any
read-only page. These helpers use supervisor identity aliases internally;
drivers and other kernel code do not need to dereference raw user pointers.

## Mapping API

The API accepts aligned virtual addresses from `0x40000000` up to, but excluding,
`0x7fc00000`, allocated physical frames, and writable or read-only permissions. The
[heap](storage.md#heap) reserves `0x40000000`–`0x403fffff`; other callers must
use addresses starting at `RUM_KERNEL_ALIAS_BASE` (`0x40400000`). Reserved
kernel-stack, user, and unassigned ranges are rejected without allocating tables.

`paging_map_page` preserves the identity window, rejects existing mappings,
and creates shared tables on demand. Mapping and unmapping require the kernel
space as their owner; calls using other spaces are rejected. `paging_translate`
can inspect any registered space and returns a physical address including the
byte offset.

`paging_unmap_page` removes a mapping and can return its frame, but does not
free the data frame. Empty dynamic tables are removed and their frames freed.
Mapping changes invalidate TLB entries and preserve interrupt flags.

Remove all dynamic aliases before freeing a frame, and never free active directory
or table frames. Read-only aliases still have writable identity mappings;
they do not provide isolation from other kernel code.

```c
uint32_t frame = pmm_allocate_page();
struct paging_space *space = paging_kernel_space();
if (frame && paging_map_page(space, RUM_KERNEL_ALIAS_BASE, frame, PAGING_WRITABLE)) {
    *(volatile uint32_t *)RUM_KERNEL_ALIAS_BASE = 42;
    paging_unmap_page(space, RUM_KERNEL_ALIAS_BASE, NULL);
    pmm_free_page(frame);
} else if (frame) {
    pmm_free_page(frame);
}
```

## Tests

Host tests cover map extensions, overlaps, rounding, boot reservations,
malformed input, exhaustion, reuse, and invalid frees. They run the allocator
with simulated boot structures and stubbed privileged instructions.

QEMU tests derive eligible pages from the actual Multiboot map, compare
bitmaps, and inspect frame ownership, page tables, permissions, the null guard,
and CR0/3/4. Boots at 16, 64, 256, and 1152 MiB check behavior across RAM sizes
and the 1 GiB limit.

Isolated kernels test aliases, reserved ranges, translation, remapping, table
accounting, and allocation-failure recovery. Context tests switch between two
directories, grow the heap after creation, publish and retire new kernel
tables, verify real CR3 values and live timer IRQs, and check bounded/repeated
creation and destruction. Directory and metadata exhaustion must recover
without leaking owned resources; retained heap pages are accounted separately.

User-memory cases allocate the same virtual program and stack pages in two
spaces, verify complete zero-fill and distinct physical frames, cross page-table
boundaries, change permissions, and exercise checked buffers and strings at
holes and end addresses. Forced PMM exhaustion checks rollback after every
partial acquisition. QMP then walks the live directories and tables, rebuilds
the allocation ledger from owners, and compares it byte-for-byte with the PMM
bitmap.

Fault cases cover null reads, code/constant
writes, unmapped aliases, and read-only alias writes. They verify CR2, error
codes, faulting EIP, write protection, and the panic halt.
Null and code/constant protection cases run under child CR3s. The serial markers
`rum_user_memory_ok`, `rum_paging_spaces_ok`, and `rum_paging_fault_space_ok`
confirm those checks ran.
Reports and dumps are in `build/test-artifacts/`.

## References

- [Multiboot v1 specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
- [GRUB Multiboot ABI header](https://github.com/rhboot/grub2/blob/master/include/multiboot.h)
- [Intel system programming manual](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf)
