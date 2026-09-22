# Memory management

rum uses the Multiboot v1 memory map for physical allocation and plain 32-bit,
non-PAE paging for protection and address spaces. Both physical management and
the kernel identity window stop at 1 GiB.

See [Memory map and ownership](memory-layout.md) for the address ranges and
release rules summarized by this guide.

## Initialization order

Memory services have a strict startup order:

1. `pmm_initialize` parses and reserves the Multiboot memory map.
2. `paging_initialize` allocates the kernel directory and enables CR0.PG/WP.
3. `heap_initialize` maps the first heap page.
4. Additional address spaces, user pages, guarded stacks, and RAM files may then
   be created.

Do not call a later layer while one of its dependencies is uninitialized.

## Physical page allocator

Only complete 4 KiB pages from Multiboot type-1 regions are eligible. Reserved
entries override overlapping available entries. RAM above 1 GiB, partial pages,
the first MiB, the kernel, and all referenced bootloader structures are excluded.

The allocator keeps separate managed and allocated bitmaps in kernel BSS. The
managed count answers “could this page ever be allocated?”; the free count
answers “is it available now?”

| API | Use |
| --- | --- |
| `pmm_allocate_page()` | Own one physical page; returns zero on exhaustion |
| `pmm_free_page(address)` | Release one owned page; rejects invalid, reserved, or double frees |
| `pmm_allocate_contiguous(pages)` | Own a physically adjacent run when a device or format requires it |
| `pmm_free_contiguous(address, pages)` | Release an exactly owned contiguous run |
| `pmm_is_managed` / `pmm_is_allocated` | Inspect page-ledger state |
| `pmm_stats()` | Read usable, managed, free, and address-limit totals |

Allocated PMM pages contain unspecified bytes. A higher-level API must clear
them before exposing them to user mode or treating them as an empty page table.
All PMM operations preserve the caller's interrupt state and assume one CPU.

## Kernel paging

The kernel directory identity maps `0x1000` through the detected RAM window.
Page zero stays absent. Kernel text and read-only constants are read-only;
kernel data, BSS, page tables, stacks, and the VGA window are writable. CR0.WP
makes those read-only permissions apply in ring 0 too.

All kernel mappings are supervisor-only. VGA and ROM addresses in
`0xa0000`–`0x100000` use cache-disable. PAE, large pages, global pages, demand
paging, swap, and execute-disable are not used.

The permanent kernel space owns these shared mappings. Every private space has
its own directory frame but borrows the kernel directory entries below
`RUM_USER_BASE`. Creating or retiring a shared kernel page table updates every
registered directory.

## Address spaces

`struct paging_space` is opaque. Treat a returned pointer as a registered handle,
not as page-table memory.

| API | Use |
| --- | --- |
| `paging_kernel_space()` | Get the permanent kernel owner |
| `paging_active_space()` | Get the handle whose directory is loaded in CR3 |
| `paging_directory_address(space)` | Read a registered space's physical CR3 value |
| `paging_space_stats(space, &stats)` | Read that space's directory and private ownership counts |
| `paging_space_create()` | Create an empty private user space borrowing kernel mappings |
| `paging_switch_space(space)` | Load a registered directory and update active bookkeeping |
| `paging_space_destroy(space)` | Destroy an inactive private space and all of its private pages |
| `paging_stats()` | Read global space, table, and user-page totals |

At most 16 private spaces can coexist. Creation requires the heap because the
registry metadata lives there. Destruction rejects the kernel space, active
space, unknown handles, and malformed ownership state. A handle becomes invalid
after successful destruction.

Reloading CR3 on every switch discards non-global cached translations. This is
also how changes made while a space was inactive become visible when it runs.

## Private user mappings

User pages may be mapped only in the program range
`0x80000000`–`0xbfc00000` or the fixed stack range
`0xbfff0000`–`0xc0000000`. Requests cannot cross between ranges. The stack guard,
reserved gap, page zero, and addresses at or above `0xc0000000` are rejected.

| API | Use |
| --- | --- |
| `paging_user_allocate(space, address, pages, flags)` | Allocate, zero, own, and map a complete page range |
| `paging_user_protect(space, address, pages, flags)` | Change write permission after validating the whole range |
| `paging_user_release(space, address, pages)` | Unmap and free a complete owned range, then reclaim empty tables |
| `paging_user_page_count(space)` | Read the space's charged data-page count |

The per-space private-page budget is 16 MiB including the user stack. Allocation
publishes nothing until each new frame is zero and its table is valid. If any
step fails, only pages and tables acquired by that call are removed; earlier
mappings stay intact.

The i386 mode used by rum has no NX bit. Read-only pages are protected from
writes, but executable ELF validation must reject writable executable segments
in software.

## Safe user access

Kernel code must never directly trust a pointer supplied by user mode. Use the
checked helpers:

| API | Use |
| --- | --- |
| `paging_user_accessible(space, address, bytes, writable)` | Validate the complete range and requested permission |
| `paging_copy_from_user(space, destination, source, bytes)` | Validate all source pages, then copy into kernel memory |
| `paging_copy_to_user(space, destination, source, bytes)` | Validate all destination pages, then copy from kernel memory |
| `paging_copy_string_from_user(...)` | Copy a NUL-terminated string within a fixed capacity |

Nonempty ranges reject overflow, holes, guards, addresses outside the user
window, and a write touching any read-only page. Validation completes before
the first byte is copied, so failure does not leave a partial destination.
Zero-length operations succeed for a valid private space without inspecting
the address.

## Guarded kernel stacks

`paging_kernel_stack_allocate(slot)` maps four cleared supervisor pages above
that slot's unmapped guard. It is transactional: a failure returns all newly
acquired frames and leaves the slot empty.

`paging_kernel_stack_release(slot)` validates every mapping, unmaps and frees
the four frames, and reclaims the shared table only when it becomes empty. The
task system must switch away before calling it. Stack mappings are shared into
all spaces because CR3 and ESP can change during the same context switch.

## General kernel aliases

The heap owns `0x40000000`–`0x40400000`. Other temporary or long-lived kernel
aliases start at `RUM_KERNEL_ALIAS_BASE` and end before the stack window.

```c
uint32_t frame = pmm_allocate_page();
struct paging_space *kernel = paging_kernel_space();

if (frame && paging_map_page(kernel, RUM_KERNEL_ALIAS_BASE,
                             frame, PAGING_WRITABLE)) {
    *(volatile uint32_t *)RUM_KERNEL_ALIAS_BASE = 42;
    paging_unmap_page(kernel, RUM_KERNEL_ALIAS_BASE, NULL);
    pmm_free_page(frame);
} else if (frame) {
    pmm_free_page(frame);
}
```

`paging_map_page` rejects overlaps and creates a shared table when needed.
`paging_unmap_page` removes the alias and optionally returns the physical
address; it does not free the data frame. `paging_translate` can inspect any
registered space and includes the original byte offset in its result.

A read-only alias does not make the same physical frame immutable through its
writable identity mapping. These permissions protect interfaces and accidental
access; kernel code still has full responsibility for its aliases.

## Diagnosing memory problems

- Use `diag` to compare physical free pages, paging ownership, task ownership,
  and active/record CR3 values.
- A lower free-page count after heap churn is normal when mapped heap capacity
  grew; freed heap blocks remain available inside the heap.
- A failed mapping call should leave both PMM and paging counts unchanged.
- A repeated process lifecycle should return directory, private-table, user-page,
  and guarded-stack totals to the same baseline after reaping.
- A page fault report's CR2 is the accessed address; its error bits distinguish
  absence from a write-protection violation.

Run `make test` after changing the Multiboot parser, PMM ledger, page-table
ownership, permissions, TLB invalidation, user-copy rules, or rollback paths.

## References

- [Multiboot v1 specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
- [Intel Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
