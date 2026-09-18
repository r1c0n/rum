# Memory management

rum uses GRUB's Multiboot v1 memory map to allocate physical RAM and enable
32-bit, non-PAE paging. Physical allocation and the identity window are limited
to addresses below `0x40000000` (1 GiB).

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

All mappings are supervisor-only. Kernel `.text` and `.rodata` are read-only;
data, BSS, stack, and page tables are writable. Legacy VGA/ROM pages from
`0xa0000` to `0x100000` have caching disabled.

CR3 points to the physical directory. CR4 clears PAE, large-page, and global-page
modes. CR0 enables PG and WP so ring-0 writes to read-only pages fault. This
paging mode has no NX bit. User address spaces, demand paging, and swap are
unsupported.

Initialization failure frees allocated tables and leaves paging disabled.
Invalid boot maps or paging failures are reported before the kernel halts.

## Mapping API

The API accepts aligned virtual addresses at or above `0x40000000`, allocated
physical frames, and writable or read-only permissions. The
[heap](storage.md#heap) reserves `0x40000000`–`0x403fffff`; other callers must
use addresses outside that range.

`paging_map_page` preserves the identity window, rejects existing mappings,
and creates tables on demand. `paging_translate` returns a physical address
including the byte offset.

`paging_unmap_page` removes a mapping and can return its frame, but does not
free the data frame. Empty dynamic tables are removed and their frames freed.
Mapping changes invalidate TLB entries and preserve interrupt flags.

Remove all aliases before freeing a frame, and never free active directory
or table frames. Read-only aliases still have writable identity mappings;
they do not provide isolation from other kernel code.

```c
uint32_t frame = pmm_allocate_page();
if (frame && paging_map_page(0x40400000, frame, PAGING_WRITABLE)) {
    *(volatile uint32_t *)0x40400000 = 42;
    paging_unmap_page(0x40400000, NULL);
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

Isolated kernels test aliases, translation, remapping, table accounting, and
allocation-failure recovery. Fault cases cover null reads, code/constant
writes, unmapped aliases, and read-only alias writes. They verify CR2, error
codes, faulting EIP, write protection, and the panic halt.
Reports and dumps are in `build/test-artifacts/`.

## References

- [Multiboot v1 specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
- [GRUB Multiboot ABI header](https://github.com/rhboot/grub2/blob/master/include/multiboot.h)
- [Intel system programming manual](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf)
