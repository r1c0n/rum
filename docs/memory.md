# Physical pages and paging (milestone 5)

rum uses GRUB's Multiboot v1 memory map to allocate physical RAM and enable
32-bit, non-PAE paging. Version `0.1.0` remains unchanged. Normal boot reports
the memory map, allocator, and paging before entering the existing command loop.

## Physical allocator

`include/rum/multiboot.h` describes the wire layout with fixed-width physical
addresses. The parser requires information flag 6 and a nonempty map; it does
not infer free RAM from `mem_upper`. Each entry's size describes the bytes after
its size field, so larger future entries are skipped correctly. Truncation,
undersized entries, address overflow, and invalid metadata lengths fail closed.
Boot information is trusted to reside in readable physical memory supplied by
the bootloader; this parser is not a fault-catching reader for arbitrary pointers.

Only type-1 RAM contributes whole 4096-byte pages. Partial pages are excluded,
and overlapping reserved/ACPI/NVS/bad-memory entries override available entries.
The initial architecture manages physical addresses below `0x40000000` (1 GiB).
RAM above this limit is ignored without wrapping into lower addresses.

Two fixed bitmaps in kernel BSS use 64 KiB total: one marks managed pages, the
other marks allocated pages. Initialization reserves:

- The first MiB, including page zero, firmware, and legacy device memory.
- The entire linker-defined kernel range, including BSS, stack, GDT, IDT,
  driver state, and allocator bitmaps.
- Multiboot information, memory-map storage, command/loader strings, modules
  and their strings/payloads, symbol tables and loaded ELF section payloads.
- Reported drive, BIOS configuration, APM/VBE, framebuffer, and palette data.

Strings must terminate within 4096 bytes. Reservations block every touched page,
even for unaligned or overlapping objects. The current milestone keeps boot
resources reserved for their lifetime rather than trying to reclaim them early.
Kernel pages must be described as usable RAM before reservation.

`pmm_allocate_page()` returns an aligned physical address, or zero on exhaustion
or failed initialization. Returned contents are uninitialized. `pmm_free_page()`
rejects unaligned/out-of-range addresses, permanently reserved pages, unallocated
pages, and double frees. Allocation, freeing, and statistics snapshots briefly
save/disable/restore interrupts. This is a single-CPU allocator, without SMP locks.

`pmm_stats()` reports usable pages before permanent reservations, managed pages
after reservations, free pages, and the end of the physical RAM window. Allocated
page directories and tables count against free pages. The serial boot report
`rum_memory_ok` records these values and the boot information/directory addresses.

## Paging

`arch/i386/paging.c` obtains the page directory and every page table from the
allocator, clears them, and uses 1024-entry, two-level 4 KiB paging. It identity
maps addresses from `0x1000` to the RAM-window limit, including gaps/reservations
inside that window. Those mappings allow physical access; the allocator still
never hands out holes or reserved pages. Page zero remains absent.

All mappings are supervisor-only. Kernel `.text` and `.rodata` pages are
read-only; data, BSS, stack, and page tables are writable. Legacy VGA/ROM pages
from `0xa0000` to `0x100000` have cache-disable set. CR3 points at the physical
directory. CR4 clears PAE, large-page, and global-page modes; CR0 sets PG and WP,
so ring-0 writes to read-only pages fault too. This non-PAE stage has no NX bit,
user address spaces, demand paging or swap. A page-backed kernel heap now uses
the first 4 MiB of the dynamic window; see [heap and RAM files](storage.md).

If initialization cannot allocate all tables, it frees the directory and every
table, leaves paging off, and returns failure. Normal boot then reports the
failure and halts. Missing/invalid memory maps similarly halt before IRQs enable.

The mapping API accepts aligned virtual addresses at/above `0x40000000`, aligned
physical frames currently owned by the allocator, and either writable or
read-only permissions. It preserves the identity window and rejects existing
mappings rather than overwriting them. New tables are allocated on demand.
`paging_translate()` returns the physical address including a byte offset.

`paging_unmap_page()` removes an alias and optionally returns its physical frame.
It does not free that data frame. Empty dynamic tables are removed, the TLB is
flushed, and their physical frames are freed. Map/unmap operations invalidate
translations and preserve interrupt flags. Callers must remove all their aliases
before freeing a data frame, and must never free the directory or table frames.
Read-only aliases still have writable physical identity access; they are not
separate protected user address spaces.

```c
/* Start after the heap's reserved 0x40000000..0x403fffff window. */
uint32_t frame = pmm_allocate_page();
if (frame && paging_map_page(0x40400000, frame, PAGING_WRITABLE)) {
    *(volatile uint32_t *)0x40400000 = 42;
    paging_unmap_page(0x40400000, NULL);
    pmm_free_page(frame);
} else if (frame) {
    pmm_free_page(frame);
}
```

## Verification

`./rum.ps1 test` / `make test` run synthetic map tests for extensions, overlap,
rounding, boot/module/symbol reservations, high RAM, malformed input, exhaustion,
reuse, and invalid frees. Host tests exercise the real allocator with simulated
low-address boot structures; privileged interrupt instructions are stubbed.

In normal QEMU boots, tests independently derive eligible pages from the actual
Multiboot map and metadata, compare the allocator bitmap, verify table-frame
ownership/accounting (including heap data frames), and inspect all mappings,
flags, null guard, and CR0/3/4. Heap headers and boot files are also checked.
GRUB ISO and direct ELF boots also run the keyboard, timer, PIC, and command tests
with production paging enabled. Additional boots cover 16, 256, and 1152 MiB,
with a GRUB 16 MiB boot and an explicit check of the 1 GiB management limit.

An isolated kernel exercises writable/read-only aliases, byte translation,
map rejection, unmap/remap TLB behavior, shared/empty table accounting, and
partial initialization/runtime out-of-memory recovery. Five further kernels
generate actual #PFs: null read, code write, constant write, unmapped alias read,
and read-only alias write. They check error codes, CR2, faulting EIP, PG/WP,
panic output, and halt. Existing CPU-register/IRQ tests stay included. Generated
reports, logs, memory dumps, and screenshots are in `build/test-artifacts/`.

References: [Multiboot v1 specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html),
[GRUB's Multiboot ABI header](https://github.com/rhboot/grub2/blob/master/include/multiboot.h),
and [Intel system programming manual](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf)
(page tables, CR0.WP, and TLB invalidation).
