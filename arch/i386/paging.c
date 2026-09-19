#include <stddef.h>
#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/process_limits.h>

#define PRESENT 1u
#define USER 4u
#define CACHE_DISABLE 0x10u
#define FRAME_MASK 0xFFFFF000u
#define ENTRIES 1024u
#define KERNEL_ENTRIES (RUM_USER_BASE / RUM_PAGE_TABLE_SPAN)
#define USER_ENTRIES (RUM_USER_END / RUM_PAGE_TABLE_SPAN)
#define PRIVATE_TABLE_WORDS ((USER_ENTRIES - KERNEL_ENTRIES + 31u) / 32u)
#define USER_PAGE_LIMIT (RUM_PROCESS_USER_BYTES / PAGE_SIZE)
#define STACK_PAGES (RUM_KERNEL_STACK_SIZE / PAGE_SIZE)

extern const char __text_start[], __text_end[], __rodata_start[], __rodata_end[];
struct paging_space {
    uint32_t directory_physical;
    struct paging_space *next;
    uint32_t user_pages, user_tables;
    uint32_t private_tables[PRIVATE_TABLE_WORDS];
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

static bool private_table_owned(const struct paging_space *space, uint32_t index)
{
    if (index < KERNEL_ENTRIES || index >= USER_ENTRIES) return false;
    uint32_t bit = index - KERNEL_ENTRIES;
    return (space->private_tables[bit / 32] & (1u << (bit % 32))) != 0;
}

static void private_table_set(struct paging_space *space, uint32_t index, bool owned)
{
    uint32_t bit = index - KERNEL_ENTRIES, mask = 1u << (bit % 32);
    if (owned) space->private_tables[bit / 32] |= mask;
    else space->private_tables[bit / 32] &= ~mask;
}

static bool user_page_range(uint32_t virtual, uint32_t pages)
{
    if (!pages || virtual % PAGE_SIZE) return false;
    uint64_t end = (uint64_t)virtual + (uint64_t)pages * PAGE_SIZE;
    return (virtual >= RUM_USER_BASE && end <= RUM_USER_PROGRAM_END) ||
           (virtual >= RUM_USER_STACK_BASE && end <= RUM_USER_STACK_TOP);
}

static bool private_table_valid(const struct paging_space *space, uint32_t index)
{
    uint32_t entry = directory(space)[index];
    return private_table_owned(space, index) && (entry & (FRAME_MASK | 0x67u)) == entry &&
           (entry & (PRESENT | PAGING_WRITABLE | USER)) == (PRESENT | PAGING_WRITABLE | USER);
}

static uint32_t *user_entry(const struct paging_space *space, uint32_t virtual)
{
    uint32_t index = virtual >> 22;
    if (!private_table_valid(space, index)) return NULL;
    uint32_t *entry = &table(directory(space)[index])[(virtual >> 12) & 1023];
    return (*entry & (PRESENT | USER)) == (PRESENT | USER) ? entry : NULL;
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

static bool kernel_table_valid(uint32_t index)
{
    uint32_t entry = directory(&kernel_space)[index];
    return (entry & (FRAME_MASK | 0x63u)) == entry &&
           (entry & (PRESENT | PAGING_WRITABLE | USER)) == (PRESENT | PAGING_WRITABLE);
}

static bool table_empty(const uint32_t *entries)
{
    for (uint32_t i = 0; i < ENTRIES; ++i)
        if (entries[i]) return false;
    return true;
}

static void reclaim_empty_kernel_table(uint32_t index)
{
    if (!(directory(&kernel_space)[index] & PRESENT)) return;
    uint32_t *entries = table(directory(&kernel_space)[index]);
    if (!table_empty(entries)) return;
    uint32_t frame = directory(&kernel_space)[index] & FRAME_MASK;
    publish_kernel_entry(index, 0);
    /* Discard cached table references before reusing its frame. */
    reload_directory(active_space);
    if (!pmm_free_page(frame)) cpu_halt();
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
        for (const struct paging_space *space = kernel_space.next; space; space = space->next) {
            stats.private_table_pages += space->user_tables;
            stats.user_pages += space->user_pages;
        }
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
            bool valid = true;
            uint32_t pages = 0, tables = 0;
            for (uint32_t i = KERNEL_ENTRIES; i < ENTRIES; ++i) {
                bool present = (directory(space)[i] & PRESENT) != 0;
                bool owned = private_table_owned(space, i);
                if (i >= USER_ENTRIES) {
                    if (directory(space)[i] || owned) valid = false;
                    continue;
                }
                if ((!present && directory(space)[i]) || present != owned ||
                    (present && !private_table_valid(space, i))) {
                    valid = false;
                    continue;
                }
                if (!owned) continue;
                ++tables;
                const uint32_t *entries = table(directory(space)[i]);
                for (uint32_t j = 0; j < ENTRIES; ++j) {
                    if (!(entries[j] & PRESENT)) {
                        if (entries[j]) valid = false;
                        continue;
                    }
                    if ((entries[j] & (FRAME_MASK | 0x67u)) != entries[j] || !(entries[j] & USER) ||
                        !pmm_is_allocated(entries[j] & FRAME_MASK)) valid = false;
                    ++pages;
                }
            }
            if (valid && pages == space->user_pages && tables == space->user_tables) {
                previous->next = space->next;
                --private_spaces;
                for (uint32_t i = KERNEL_ENTRIES; i < USER_ENTRIES; ++i) {
                    if (!private_table_owned(space, i)) continue;
                    uint32_t *entries = table(directory(space)[i]);
                    for (uint32_t j = 0; j < ENTRIES; ++j)
                        if ((entries[j] & PRESENT) && !pmm_free_page(entries[j] & FRAME_MASK)) cpu_halt();
                    if (!pmm_free_page(directory(space)[i] & FRAME_MASK)) cpu_halt();
                }
                if (!pmm_free_page(space->directory_physical) || !kfree(space)) cpu_halt();
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
            reclaim_empty_kernel_table(index);
        }
    }
    cpu_interrupt_restore(saved);
    return success;
}

static bool kernel_stack_range(uint32_t slot, uint32_t *base)
{
    if (slot >= RUM_KERNEL_STACK_SLOTS) return false;
    uint32_t address = RUM_KERNEL_STACK_SLOT_BASE(slot);
    if (address < RUM_KERNEL_STACK_BASE ||
        address > RUM_KERNEL_STACK_END - RUM_KERNEL_STACK_SIZE) return false;
    if (base) *base = address;
    return true;
}

bool paging_kernel_stack_allocate(uint32_t slot)
{
    uint32_t base;
    if (!enabled || !kernel_stack_range(slot, &base)) return false;
    uint32_t saved = cpu_interrupt_save(), index = base >> 22;
    uint32_t guard = RUM_KERNEL_STACK_GUARD(slot);
    bool valid = !paging_translate(&kernel_space, guard, NULL);
    uint32_t existing = directory(&kernel_space)[index];
    if (existing && !kernel_table_valid(index)) valid = false;
    if (valid && existing) {
        uint32_t *entries = table(existing);
        if (entries[(guard >> 12) & 1023]) valid = false;
        for (uint32_t page = 0; page < STACK_PAGES; ++page)
            if (entries[((base >> 12) & 1023) + page]) valid = false;
    }
    bool created_table = false;
    if (valid && !existing) {
        uint32_t frame = pmm_allocate_page();
        if (frame) {
            memset((void *)(uintptr_t)frame, 0, PAGE_SIZE);
            publish_kernel_entry(index, frame | PRESENT | PAGING_WRITABLE);
            created_table = true;
        } else valid = false;
    }
    uint32_t mapped = 0;
    while (valid && mapped < STACK_PAGES) {
        uint32_t frame = pmm_allocate_page();
        if (!frame) break;
        memset((void *)(uintptr_t)frame, 0, PAGE_SIZE);
        uint32_t address = base + mapped * PAGE_SIZE;
        table(directory(&kernel_space)[index])[(address >> 12) & 1023] =
            frame | PRESENT | PAGING_WRITABLE;
        if (active_space) invalidate(address);
        ++mapped;
    }
    bool success = valid && mapped == STACK_PAGES;
    if (!success) {
        while (mapped) {
            --mapped;
            uint32_t address = base + mapped * PAGE_SIZE;
            uint32_t *entry = &table(directory(&kernel_space)[index])[(address >> 12) & 1023];
            uint32_t frame = *entry & FRAME_MASK;
            *entry = 0;
            if (active_space) invalidate(address);
            if (!pmm_free_page(frame)) cpu_halt();
        }
        if (created_table) reclaim_empty_kernel_table(index);
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_kernel_stack_release(uint32_t slot)
{
    uint32_t base;
    if (!enabled || !kernel_stack_range(slot, &base)) return false;
    uint32_t saved = cpu_interrupt_save(), index = base >> 22;
    bool valid = kernel_table_valid(index) &&
                 !paging_translate(&kernel_space, RUM_KERNEL_STACK_GUARD(slot), NULL);
    uint32_t *entries = valid ? table(directory(&kernel_space)[index]) : NULL;
    for (uint32_t page = 0; valid && page < STACK_PAGES; ++page) {
        uint32_t entry = entries[((base >> 12) & 1023) + page];
        if ((entry & (FRAME_MASK | 0x63u)) != entry ||
            (entry & (PRESENT | PAGING_WRITABLE | USER)) != (PRESENT | PAGING_WRITABLE) ||
            !pmm_is_allocated(entry & FRAME_MASK)) valid = false;
    }
    if (valid) {
        for (uint32_t page = 0; page < STACK_PAGES; ++page) {
            uint32_t address = base + page * PAGE_SIZE;
            uint32_t *entry = &entries[(address >> 12) & 1023];
            uint32_t frame = *entry & FRAME_MASK;
            *entry = 0;
            if (active_space) invalidate(address);
            if (!pmm_free_page(frame)) cpu_halt();
        }
        reclaim_empty_kernel_table(index);
    }
    cpu_interrupt_restore(saved);
    return valid;
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

static bool ensure_user_table(struct paging_space *space, uint32_t index)
{
    if (directory(space)[index]) return private_table_valid(space, index);
    uint32_t frame = pmm_allocate_page();
    if (!frame) return false;
    memset((void *)(uintptr_t)frame, 0, PAGE_SIZE);
    directory(space)[index] = frame | PRESENT | PAGING_WRITABLE | USER;
    private_table_set(space, index, true);
    ++space->user_tables;
    return true;
}

static void reclaim_empty_user_tables(struct paging_space *space, uint32_t virtual, uint32_t pages)
{
    uint32_t first = virtual >> 22;
    uint32_t last = (uint32_t)(((uint64_t)virtual + (uint64_t)pages * PAGE_SIZE - 1) >> 22);
    for (uint32_t index = first; index <= last; ++index) {
        if (!private_table_owned(space, index)) continue;
        uint32_t *entries = table(directory(space)[index]);
        bool empty = true;
        for (uint32_t i = 0; i < ENTRIES; ++i)
            if (entries[i] & PRESENT) { empty = false; break; }
        if (!empty) continue;
        uint32_t frame = directory(space)[index] & FRAME_MASK;
        directory(space)[index] = 0;
        private_table_set(space, index, false);
        --space->user_tables;
        if (!pmm_free_page(frame)) cpu_halt();
    }
}

bool paging_user_allocate(struct paging_space *space, uint32_t virtual,
                          uint32_t pages, uint32_t flags)
{
    if (!user_page_range(virtual, pages) || flags & ~PAGING_WRITABLE) return false;
    uint32_t saved = cpu_interrupt_save();
    bool valid = known_space(space) && space != &kernel_space &&
                 space->user_pages <= USER_PAGE_LIMIT &&
                 pages <= USER_PAGE_LIMIT - space->user_pages;
    for (uint32_t i = 0; valid && i < pages; ++i) {
        uint32_t address = virtual + i * PAGE_SIZE;
        uint32_t index = address >> 22;
        if (((directory(space)[index] != 0) != private_table_owned(space, index)) ||
            (directory(space)[index] && !private_table_valid(space, index))) valid = false;
        else if (directory(space)[index]) {
            uint32_t entry = table(directory(space)[index])[(address >> 12) & 1023];
            if (entry) valid = false;
        }
    }
    uint32_t mapped = 0;
    while (valid && mapped < pages) {
        uint32_t address = virtual + mapped * PAGE_SIZE;
        uint32_t index = address >> 22;
        if (!ensure_user_table(space, index)) break;
        uint32_t physical = pmm_allocate_page();
        if (!physical) break;
        memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
        table(directory(space)[index])[(address >> 12) & 1023] =
            physical | PRESENT | USER | flags;
        if (space == active_space) invalidate(address);
        ++space->user_pages;
        ++mapped;
    }
    bool success = valid && mapped == pages;
    if (!success) {
        for (uint32_t i = 0; i < mapped; ++i) {
            uint32_t address = virtual + i * PAGE_SIZE;
            uint32_t *entry = user_entry(space, address);
            uint32_t physical = *entry & FRAME_MASK;
            *entry = 0;
            if (space == active_space) invalidate(address);
            if (!pmm_free_page(physical)) cpu_halt();
            --space->user_pages;
        }
        if (valid) reclaim_empty_user_tables(space, virtual, pages);
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_user_protect(struct paging_space *space, uint32_t virtual,
                         uint32_t pages, uint32_t flags)
{
    if (!user_page_range(virtual, pages) || flags & ~PAGING_WRITABLE) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = known_space(space) && space != &kernel_space;
    for (uint32_t i = 0; success && i < pages; ++i)
        if (!user_entry(space, virtual + i * PAGE_SIZE)) success = false;
    if (success) {
        for (uint32_t i = 0; i < pages; ++i) {
            uint32_t address = virtual + i * PAGE_SIZE;
            uint32_t *entry = user_entry(space, address);
            *entry = (*entry & (FRAME_MASK | PRESENT | USER | 0x60u)) | flags;
            if (space == active_space) invalidate(address);
        }
    }
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_user_release(struct paging_space *space, uint32_t virtual, uint32_t pages)
{
    if (!user_page_range(virtual, pages)) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = known_space(space) && space != &kernel_space;
    for (uint32_t i = 0; success && i < pages; ++i)
        if (!user_entry(space, virtual + i * PAGE_SIZE)) success = false;
    if (success) {
        for (uint32_t i = 0; i < pages; ++i) {
            uint32_t address = virtual + i * PAGE_SIZE;
            uint32_t *entry = user_entry(space, address);
            uint32_t physical = *entry & FRAME_MASK;
            *entry = 0;
            if (space == active_space) invalidate(address);
            if (!pmm_free_page(physical)) cpu_halt();
            --space->user_pages;
        }
        reclaim_empty_user_tables(space, virtual, pages);
    }
    cpu_interrupt_restore(saved);
    return success;
}

uint32_t paging_user_page_count(const struct paging_space *space)
{
    uint32_t saved = cpu_interrupt_save();
    uint32_t pages = known_space(space) && space != &kernel_space ? space->user_pages : 0;
    cpu_interrupt_restore(saved);
    return pages;
}

static bool user_bytes(const struct paging_space *space, uint32_t address,
                       size_t bytes, bool writable)
{
    if (!known_space(space) || space == &kernel_space) return false;
    if (!bytes) return true;
    if (address < RUM_USER_BASE || address >= RUM_USER_END || bytes > RUM_USER_END - address)
        return false;
    uint32_t first = address & FRAME_MASK;
    uint32_t last = (uint32_t)((address + bytes - 1) & FRAME_MASK);
    for (uint32_t page = first;; page += PAGE_SIZE) {
        uint32_t *entry = user_entry(space, page);
        if (!entry || (writable && !(*entry & PAGING_WRITABLE))) return false;
        if (page == last) break;
    }
    return true;
}

bool paging_user_accessible(const struct paging_space *space, uint32_t address,
                            size_t bytes, bool writable)
{
    uint32_t saved = cpu_interrupt_save();
    bool success = user_bytes(space, address, bytes, writable);
    cpu_interrupt_restore(saved);
    return success;
}

static void copy_user_bytes(const struct paging_space *space, uint32_t user,
                            void *kernel, size_t bytes, bool to_user)
{
    unsigned char *buffer = kernel;
    while (bytes) {
        uint32_t *entry = user_entry(space, user);
        size_t offset = user & (PAGE_SIZE - 1);
        size_t chunk = PAGE_SIZE - offset;
        if (chunk > bytes) chunk = bytes;
        void *physical = (void *)(uintptr_t)((*entry & FRAME_MASK) + offset);
        if (to_user) memmove(physical, buffer, chunk);
        else memmove(buffer, physical, chunk);
        user += (uint32_t)chunk;
        buffer += chunk;
        bytes -= chunk;
    }
}

bool paging_copy_from_user(const struct paging_space *space, void *destination,
                           uint32_t source, size_t bytes)
{
    if (bytes && !destination) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = user_bytes(space, source, bytes, false);
    if (success && bytes) copy_user_bytes(space, source, destination, bytes, false);
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_copy_to_user(const struct paging_space *space, uint32_t destination,
                         const void *source, size_t bytes)
{
    if (bytes && !source) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = user_bytes(space, destination, bytes, true);
    if (success && bytes) copy_user_bytes(space, destination, (void *)source, bytes, true);
    cpu_interrupt_restore(saved);
    return success;
}

bool paging_copy_string_from_user(const struct paging_space *space, char *destination,
                                  size_t capacity, uint32_t source, size_t *length)
{
    if (!destination || !capacity) return false;
    uint32_t saved = cpu_interrupt_save();
    bool success = known_space(space) && space != &kernel_space;
    size_t bytes = 0;
    while (success && bytes < capacity) {
        if (source < RUM_USER_BASE || source >= RUM_USER_END || bytes > RUM_USER_END - source - 1) {
            success = false;
            break;
        }
        uint32_t address = source + (uint32_t)bytes;
        uint32_t *entry = user_entry(space, address);
        if (!entry) {
            success = false;
            break;
        }
        char character = *(const char *)(uintptr_t)((*entry & FRAME_MASK) + (address & (PAGE_SIZE - 1)));
        ++bytes;
        if (!character) break;
    }
    if (!bytes || bytes > capacity) success = false;
    if (success) {
        uint32_t address = source + (uint32_t)(bytes - 1);
        uint32_t *entry = user_entry(space, address);
        if (*(const char *)(uintptr_t)((*entry & FRAME_MASK) + (address & (PAGE_SIZE - 1))) != '\0')
            success = false;
    }
    if (success) {
        copy_user_bytes(space, source, destination, bytes, false);
        if (length) *length = bytes - 1;
    }
    cpu_interrupt_restore(saved);
    return success;
}
