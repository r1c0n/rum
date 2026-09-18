#include <stddef.h>
#include <rum/cpu.h>
#include <rum/memory.h>
#include <rum/paging.h>

#define PRESENT 1u
#define CACHE_DISABLE 0x10u
#define FRAME_MASK 0xFFFFF000u
#define ENTRIES 1024u

extern const char __text_start[], __text_end[], __rodata_start[], __rodata_end[];
struct paging_space {
    uint32_t directory_physical;
};

static struct paging_space kernel_space;
static struct paging_space *active_space;
static bool enabled;

static uint32_t *directory(const struct paging_space *space)
{
    return (uint32_t *)(uintptr_t)space->directory_physical;
}

static uint32_t *table(uint32_t entry)
{
    return (uint32_t *)(uintptr_t)(entry & FRAME_MASK);
}

static void invalidate(uint32_t virtual)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual) : "memory");
}

static void reload_directory(const struct paging_space *space)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->directory_physical) : "memory");
}

static bool read_only(uint32_t physical)
{
    return (physical >= (uintptr_t)__text_start && physical < (uintptr_t)__text_end) ||
           (physical >= (uintptr_t)__rodata_start && physical < (uintptr_t)__rodata_end);
}

bool paging_initialize(void)
{
    if (enabled) return false;
    uint32_t control;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(control));
    if (control & 0x80000000u) return false;
    uint32_t limit = pmm_stats().limit;
    if (!limit) return false;
    kernel_space.directory_physical = pmm_allocate_page();
    if (!kernel_space.directory_physical) return false;
    uint32_t *entries = directory(&kernel_space);
    memset(entries, 0, PAGE_SIZE);
    for (uint32_t physical = PAGE_SIZE; physical < limit; physical += PAGE_SIZE) {
        uint32_t index = physical >> 22;
        if (!(entries[index] & PRESENT)) {
            uint32_t frame = pmm_allocate_page();
            if (!frame) {
                /* Paging is still off. Roll back every allocated structure. */
                for (uint32_t i = 0; i < ENTRIES; ++i)
                    if (entries[i] & PRESENT) (void)pmm_free_page(entries[i] & FRAME_MASK);
                (void)pmm_free_page(kernel_space.directory_physical);
                kernel_space.directory_physical = 0;
                return false;
            }
            memset((void *)(uintptr_t)frame, 0, PAGE_SIZE);
            entries[index] = frame | PRESENT | PAGING_WRITABLE;
        }
        uint32_t flags = PRESENT | (read_only(physical) ? 0 : PAGING_WRITABLE);
        if (physical >= 0xA0000 && physical < 0x100000) flags |= CACHE_DISABLE;
        table(entries[index])[(physical >> 12) & 1023] = physical | flags;
    }
    uint32_t features;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(features));
    features &= ~0xB0u; /* Plain 32-bit paging: no PAE, large pages, or globals. */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(features) : "memory");
    reload_directory(&kernel_space);
    control |= 0x80010000u; /* PG and WP: enforce read-only pages even in ring 0. */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(control) : "memory");
    active_space = &kernel_space;
    enabled = true;
    return true;
}

struct paging_space *paging_kernel_space(void)
{
    return enabled ? &kernel_space : NULL;
}

struct paging_space *paging_active_space(void)
{
    return enabled ? active_space : NULL;
}

uint32_t paging_directory_address(const struct paging_space *space)
{
    return enabled && space == &kernel_space ? space->directory_physical : 0;
}

bool paging_switch_space(struct paging_space *space)
{
    uint32_t saved = cpu_interrupt_save();
    bool success = enabled && space == &kernel_space;
    if (success) {
        reload_directory(space);
        active_space = space;
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_map_page(struct paging_space *space, uint32_t virtual, uint32_t physical, uint32_t flags)
{
    if (!enabled || space != &kernel_space ||
        !memory_kernel_mapping_range(virtual, PAGE_SIZE) || virtual % PAGE_SIZE ||
        flags & ~PAGING_WRITABLE) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = false;
    if (pmm_is_allocated(physical)) {
        uint32_t index = virtual >> 22;
        if (!(directory(space)[index] & PRESENT)) {
            uint32_t frame = pmm_allocate_page();
            if (frame) {
                memset((void *)(uintptr_t)frame, 0, PAGE_SIZE);
                directory(space)[index] = frame | PRESENT | PAGING_WRITABLE;
            }
        }
        if (directory(space)[index] & PRESENT) {
            uint32_t *entry = &table(directory(space)[index])[(virtual >> 12) & 1023];
            if (!(*entry & PRESENT)) {
                *entry = physical | PRESENT | flags;
                invalidate(virtual);
                success = true;
            }
        }
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_unmap_page(struct paging_space *space, uint32_t virtual, uint32_t *physical)
{
    if (!enabled || space != &kernel_space ||
        !memory_kernel_mapping_range(virtual, PAGE_SIZE) || virtual % PAGE_SIZE)
        return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = false;
    uint32_t index = virtual >> 22;
    if (directory(space)[index] & PRESENT) {
        uint32_t *entries = table(directory(space)[index]);
        uint32_t *entry = &entries[(virtual >> 12) & 1023];
        if (*entry & PRESENT) {
            if (physical) *physical = *entry & FRAME_MASK;
            *entry = 0;
            invalidate(virtual);
            success = true;
            bool empty = true;
            for (uint32_t i = 0; i < ENTRIES; ++i)
                if (entries[i] & PRESENT) { empty = false; break; }
            if (empty) {
                uint32_t frame = directory(space)[index] & FRAME_MASK;
                directory(space)[index] = 0;
                /* Discard cached table references before reusing its frame. */
                reload_directory(active_space);
                (void)pmm_free_page(frame);
            }
        }
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_translate(const struct paging_space *space, uint32_t virtual, uint32_t *physical)
{
    if (!enabled || space != &kernel_space) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = false;
    uint32_t entry = directory(space)[virtual >> 22];
    if (entry & PRESENT) {
        entry = table(entry)[(virtual >> 12) & 1023];
        if (entry & PRESENT) {
            if (physical) *physical = (entry & FRAME_MASK) | (virtual & (PAGE_SIZE - 1));
            success = true;
        }
    }
    cpu_interrupt_restore(saved);
    return success;
}
