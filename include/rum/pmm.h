#ifndef RUM_PMM_H
#define RUM_PMM_H

#include <stdbool.h>
#include <stdint.h>
#include <rum/multiboot.h>

#define PAGE_SIZE 4096u
#define PMM_PHYSICAL_LIMIT 0x40000000u /* Initial identity window: 1 GiB. */

struct pmm_statistics {
    uint32_t usable_pages;  /* Full RAM pages before kernel/boot reservations. */
    uint32_t managed_pages; /* Pages remaining after permanent reservations. */
    uint32_t free_pages;
    uint32_t limit;         /* End of the RAM window, page-aligned. */
};

/* Startup only, with paging/interrupts off. Missing or malformed maps fail closed. */
bool pmm_initialize(const struct multiboot_info *info, uint32_t kernel_start, uint32_t kernel_end);
uint32_t pmm_allocate_page(void); /* Physical address; zero means exhausted/unready. */
bool pmm_free_page(uint32_t physical);
bool pmm_is_managed(uint32_t physical);
bool pmm_is_allocated(uint32_t physical);
struct pmm_statistics pmm_stats(void);

#endif
