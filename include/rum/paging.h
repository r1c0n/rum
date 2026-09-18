#ifndef RUM_PAGING_H
#define RUM_PAGING_H

#include <stdbool.h>
#include <stdint.h>
#include <rum/pmm.h>

#define PAGING_WRITABLE 0x02u
#define PAGING_DYNAMIC_BASE PMM_PHYSICAL_LIMIT

/* Startup only: identity-map RAM window, protect text/rodata, guard page zero. */
bool paging_initialize(void);
uint32_t paging_directory_address(void);
/* Kernel-only 4 KiB aliases above the identity window, backed by allocated RAM.
   Never overwrite existing mappings. Data frames remain owned by the caller. */
bool paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags);
bool paging_unmap_page(uint32_t virtual, uint32_t *physical);
bool paging_translate(uint32_t virtual, uint32_t *physical);

#endif
