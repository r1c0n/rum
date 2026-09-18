/* Host-only physical/paging boundary: the allocator itself is production code. */
#include <assert.h>
#include <sys/mman.h>
#include <rum/heap.h>
#include <rum/paging.h>
#include "page-backend.h"

#define FRAMES 1100u
#define PHYSICAL_BASE 0x1000000u
static bool owned[FRAMES];
static uint32_t mappings[HEAP_LIMIT / PAGE_SIZE], table_frame;
static int budget = -1;

void test_page_budget(int allocations) { budget = allocations; }
unsigned test_pages_owned(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < FRAMES; ++i) count += owned[i];
    return count;
}

uint32_t pmm_allocate_page(void)
{
    if (!budget) return 0;
    for (unsigned i = 0; i < FRAMES; ++i) {
        if (!owned[i]) {
            owned[i] = true;
            if (budget > 0) --budget;
            return PHYSICAL_BASE + i * PAGE_SIZE;
        }
    }
    return 0;
}

bool pmm_is_allocated(uint32_t physical)
{
    return physical >= PHYSICAL_BASE && physical % PAGE_SIZE == 0 &&
           (physical - PHYSICAL_BASE) / PAGE_SIZE < FRAMES &&
           owned[(physical - PHYSICAL_BASE) / PAGE_SIZE];
}

bool pmm_free_page(uint32_t physical)
{
    if (!pmm_is_allocated(physical)) return false;
    owned[(physical - PHYSICAL_BASE) / PAGE_SIZE] = false;
    return true;
}

bool paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags)
{
    assert(virtual >= HEAP_BASE && virtual < HEAP_BASE + HEAP_LIMIT);
    assert(virtual % PAGE_SIZE == 0 && flags == PAGING_WRITABLE);
    assert(pmm_is_allocated(physical));
    unsigned slot = (virtual - HEAP_BASE) / PAGE_SIZE;
    if (mappings[slot]) return false;
    if (!table_frame && !(table_frame = pmm_allocate_page())) return false;
    void *mapped = mmap((void *)(uintptr_t)virtual, PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapped == (void *)(uintptr_t)virtual);
    mappings[slot] = physical;
    return true;
}

bool paging_unmap_page(uint32_t virtual, uint32_t *physical)
{
    unsigned slot = (virtual - HEAP_BASE) / PAGE_SIZE;
    assert(slot < HEAP_LIMIT / PAGE_SIZE);
    if (!mappings[slot]) return false;
    if (physical) *physical = mappings[slot];
    mappings[slot] = 0;
    assert(munmap((void *)(uintptr_t)virtual, PAGE_SIZE) == 0);
    for (unsigned i = 0; i < HEAP_LIMIT / PAGE_SIZE; ++i)
        if (mappings[i]) return true;
    assert(pmm_free_page(table_frame));
    table_frame = 0;
    return true;
}
