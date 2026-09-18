#ifndef RUM_PAGING_H
#define RUM_PAGING_H

#include <stdbool.h>
#include <stdint.h>
#include <rum/pmm.h>

#define PAGING_WRITABLE 0x02u
#define PAGING_DYNAMIC_BASE RUM_HEAP_BASE

struct paging_space;

/* Startup only: identity-map RAM window, protect text/rodata, guard page zero. */
bool paging_initialize(void);
struct paging_space *paging_kernel_space(void);
struct paging_space *paging_active_space(void);
uint32_t paging_directory_address(const struct paging_space *space);
/* Switch CR3 while preserving interrupt flags and discarding cached translations. */
bool paging_switch_space(struct paging_space *space);
/* Kernel-only 4 KiB aliases in [RUM_HEAP_BASE, RUM_KERNEL_STACK_BASE).
   The heap owns its subrange; other callers start at RUM_KERNEL_ALIAS_BASE.
   Backed by allocated RAM; reserved stack/user ranges are rejected.
   Never overwrite existing mappings. Data frames remain owned by the caller. */
bool paging_map_page(struct paging_space *space, uint32_t virtual, uint32_t physical, uint32_t flags);
bool paging_unmap_page(struct paging_space *space, uint32_t virtual, uint32_t *physical);
bool paging_translate(const struct paging_space *space, uint32_t virtual, uint32_t *physical);

#endif
