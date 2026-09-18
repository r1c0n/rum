#include <stddef.h>
#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/process_limits.h>

#define PRESENT 1u
#define CACHE_DISABLE 0x10u
#define FRAME_MASK 0xFFFFF000u
#define ENTRIES 1024u
#define KERNEL_ENTRIES (RUM_USER_BASE / RUM_PAGE_TABLE_SPAN)

extern const char __text_start[], __text_end[], __rodata_start[], __rodata_end[];
struct paging_space {
    uint32_t directory_physical;
    struct paging_space *next;
};

static struct paging_space kernel_space;
static struct paging_space *active_space;
static bool enabled;
static uint32_t private_spaces;

/* Compare pointers before dereferencing: null and unregistered handles fail
   without touching caller memory. Registry updates run with interrupts off. */
static bool known_space(const struct paging_space *space)
{
    if (!enabled) return false;
    for (const struct paging_space *item = &kernel_space; item; item = item->next)
        if (item == space) return true;
    return false;
}

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

/* Kernel tables belong to the kernel. Publish new tables, and detach retiring
   tables, in every directory before changing the active CPU's translation. */
static void publish_kernel_entry(uint32_t index, uint32_t entry)
{
    for (struct paging_space *space = &kernel_space; space; space = space->next)
        directory(space)[index] = entry;
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

struct paging_statistics paging_stats(void)
{
    uint32_t saved = cpu_interrupt_save();
    struct paging_statistics stats = {0};
    if (enabled) {
        stats.spaces = stats.directory_pages = private_spaces + 1;
        stats.kernel_directory = kernel_space.directory_physical;
        stats.active_directory = active_space->directory_physical;
        for (uint32_t i = 0; i < KERNEL_ENTRIES; ++i)
            if (directory(&kernel_space)[i] & PRESENT) ++stats.shared_table_pages;
    }
    cpu_interrupt_restore(saved);
    return stats;
}

struct paging_space *paging_active_space(void)
{
    return enabled ? active_space : NULL;
}

uint32_t paging_directory_address(const struct paging_space *space)
{
    uint32_t saved = cpu_interrupt_save();
    uint32_t address = known_space(space) ? space->directory_physical : 0;
    cpu_interrupt_restore(saved);
    return address;
}

struct paging_space *paging_space_create(void)
{
    uint32_t saved = cpu_interrupt_save();
    struct paging_space *space = NULL;
    if (enabled && private_spaces < RUM_PROCESS_LIMIT) {
        space = kmalloc(sizeof *space);
        if (space) {
            uint32_t physical = pmm_allocate_page();
            if (!physical) {
                (void)kfree(space);
                space = NULL;
            } else {
                *space = (struct paging_space){ .directory_physical = physical,
                                               .next = kernel_space.next };
                memset(directory(space), 0, PAGE_SIZE);
                memcpy(directory(space), directory(&kernel_space), KERNEL_ENTRIES * sizeof(uint32_t));
                /* Publish only after initialization succeeds. The upper half
                   remains unmapped; private user pages are subsequent work. */
                kernel_space.next = space;
                ++private_spaces;
            }
        }
    }
    cpu_interrupt_restore(saved);
    return space;
}

bool paging_space_destroy(struct paging_space *space)
{
    uint32_t saved = cpu_interrupt_save();
    bool success = false;
    if (enabled && space && space != &kernel_space && space != active_space) {
        struct paging_space *previous = &kernel_space;
        while (previous->next && previous->next != space) previous = previous->next;
        if (previous->next) {
            /* Until private-page ownership exists, refuse unsupported private
               entries rather than leak them or guess who owns their frames. */
            bool empty = true;
            for (uint32_t i = KERNEL_ENTRIES; i < ENTRIES; ++i)
                if (directory(space)[i]) { empty = false; break; }
            if (empty) {
                previous->next = space->next;
                --private_spaces;
                (void)pmm_free_page(space->directory_physical);
                (void)kfree(space);
                success = true;
            }
        }
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_switch_space(struct paging_space *space)
{
    uint32_t saved = cpu_interrupt_save();
    bool success = known_space(space);
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
                publish_kernel_entry(index, frame | PRESENT | PAGING_WRITABLE);
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
                publish_kernel_entry(index, 0);
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
    uint32_t saved = cpu_interrupt_save();
    bool success = false;
    if (!known_space(space)) {
        cpu_interrupt_restore(saved);
        return false;
    }
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
