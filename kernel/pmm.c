#include <rum/cpu.h>
#include <rum/memory.h>
#include <rum/pmm.h>

#define PAGE_COUNT (PMM_PHYSICAL_LIMIT / PAGE_SIZE)
#define BITMAP_WORDS (PAGE_COUNT / 32)
#define ADDRESS_SPACE_END (1ull << 32)

static uint32_t managed[BITMAP_WORDS], allocated[BITMAP_WORDS];
static struct pmm_statistics statistics;
static uint32_t next_page;
static bool ready;

static bool bit(const uint32_t *bitmap, uint32_t page)
{
    return (bitmap[page / 32] & (1u << (page % 32))) != 0;
}

static void set_bit(uint32_t *bitmap, uint32_t page, bool value)
{
    uint32_t mask = 1u << (page % 32);
    if (value) bitmap[page / 32] |= mask;
    else bitmap[page / 32] &= ~mask;
}

static void range(uint64_t start, uint64_t end, bool usable)
{
    if (start >= PMM_PHYSICAL_LIMIT) return;
    if (end > PMM_PHYSICAL_LIMIT) end = PMM_PHYSICAL_LIMIT;
    /* Available RAM needs a whole page; a reservation blocks partial pages. */
    uint32_t first = (uint32_t)(usable ? (start + PAGE_SIZE - 1) / PAGE_SIZE : start / PAGE_SIZE);
    uint32_t last = (uint32_t)(usable ? end / PAGE_SIZE : (end + PAGE_SIZE - 1) / PAGE_SIZE);
    for (uint32_t page = first; page < last; ++page)
        set_bit(managed, page, usable);
}

static bool reserve(uint64_t address, uint64_t bytes)
{
    if (address >= ADDRESS_SPACE_END || bytes > ADDRESS_SPACE_END - address)
        return false;
    if (bytes) range(address, address + bytes, false);
    return true;
}

static bool reserve_string(uint32_t address)
{
    if (!address) return true;
    const char *text = (const char *)(uintptr_t)address;
    for (uint32_t size = 0; size < PAGE_SIZE && (uint64_t)address + size < ADDRESS_SPACE_END; ++size) {
        if (!text[size]) return reserve(address, size + 1);
    }
    return false;
}

static bool map_pass(const struct multiboot_info *info, bool available)
{
    uint32_t offset = 0;
    const uint8_t *map = (const uint8_t *)(uintptr_t)info->mmap_addr;
    while (offset < info->mmap_length) {
        if (info->mmap_length - offset < sizeof(struct multiboot_mmap_entry)) return false;
        struct multiboot_mmap_entry entry;
        memcpy(&entry, map + offset, sizeof entry);
        uint64_t stride = (uint64_t)entry.size + sizeof entry.size;
        if (entry.size < 20 || stride > info->mmap_length - offset ||
            entry.length > UINT64_MAX - entry.address) return false;
        if (entry.length && (entry.type == 1) == available)
            range(entry.address, entry.address + entry.length, available);
        offset += (uint32_t)stride;
    }
    return true;
}

static bool reserve_boot_data(const struct multiboot_info *info)
{
    if (!reserve((uintptr_t)info, sizeof *info) || !reserve(info->mmap_addr, info->mmap_length)) return false;
    if ((info->flags & (1u << 2)) && !reserve_string(info->cmdline)) return false;
    if ((info->flags & (1u << 9)) && !reserve_string(info->boot_loader_name)) return false;
    if (info->flags & (1u << 3)) {
        uint64_t bytes = (uint64_t)info->mods_count * sizeof(struct multiboot_module);
        if ((bytes && !info->mods_addr) || !reserve(info->mods_addr, bytes)) return false;
        const struct multiboot_module *modules = (const void *)(uintptr_t)info->mods_addr;
        for (uint32_t i = 0; i < info->mods_count; ++i) {
            if (modules[i].end < modules[i].start ||
                !reserve(modules[i].start, (uint64_t)modules[i].end - modules[i].start) ||
                !reserve_string(modules[i].string)) return false;
        }
    }
    if ((info->flags & 0x30) == 0x30) return false; /* A.out/ELF symbols are exclusive. */
    if ((info->flags & (1u << 4)) &&
        !reserve(info->symbols[2], (uint64_t)info->symbols[0] + info->symbols[1])) return false;
    if (info->flags & (1u << 5)) {
        uint64_t bytes = (uint64_t)info->symbols[0] * info->symbols[1];
        if ((bytes && (!info->symbols[2] || info->symbols[1] < 40)) ||
            !reserve(info->symbols[2], bytes)) return false;
        /* GRUB can also load symbol/string sections outside the kernel ELF's
           load segments. Keep those payloads, not just their section headers. */
        const uint8_t *sections = (const void *)(uintptr_t)info->symbols[2];
        for (uint32_t i = 0; i < info->symbols[0]; ++i) {
            uint32_t address, size;
            memcpy(&address, sections + (size_t)i * info->symbols[1] + 12, 4);
            memcpy(&size, sections + (size_t)i * info->symbols[1] + 20, 4);
            if (address && !reserve(address, size)) return false;
        }
    }
    if ((info->flags & (1u << 7)) && !reserve(info->drives_addr, info->drives_length)) return false;
    if ((info->flags & (1u << 8)) && info->config_table) {
        uint16_t bytes;
        if ((uint64_t)info->config_table + sizeof bytes > ADDRESS_SPACE_END) return false;
        memcpy(&bytes, (const void *)(uintptr_t)info->config_table, sizeof bytes);
        if (!reserve(info->config_table, (uint32_t)bytes + sizeof bytes)) return false;
    }
    if ((info->flags & (1u << 10)) && !reserve(info->apm_table, 20)) return false;
    if (info->flags & (1u << 11)) {
        if (!reserve(info->vbe_control_info, 512) || !reserve(info->vbe_mode_info, 256) ||
            !reserve((uint32_t)info->vbe_interface_seg * 16u + info->vbe_interface_off,
                     info->vbe_interface_len)) return false;
    }
    if (info->flags & (1u << 12)) {
        uint64_t bytes = (uint64_t)info->framebuffer_pitch * info->framebuffer_height;
        /* Framebuffers may be above 4 GiB; none of those pages are managed. */
        if (bytes > UINT64_MAX - info->framebuffer_addr) return false;
        if (bytes) range(info->framebuffer_addr, info->framebuffer_addr + bytes, false);
        if (info->framebuffer_type == 0) {
            uint32_t palette;
            uint16_t colors;
            memcpy(&palette, info->framebuffer_color_info, 4);
            memcpy(&colors, info->framebuffer_color_info + 4, 2);
            if (!reserve(palette, (uint32_t)colors * 3)) return false;
        }
    }
    return true;
}

bool pmm_initialize(const struct multiboot_info *info, uint32_t kernel_start, uint32_t kernel_end)
{
    ready = false;
    statistics = (struct pmm_statistics){0};
    memset(managed, 0, sizeof managed);
    memset(allocated, 0, sizeof allocated);
    if (!info || (uint64_t)(uintptr_t)info + sizeof *info > ADDRESS_SPACE_END ||
        !(info->flags & MULTIBOOT_MEMORY_MAP) || !info->mmap_addr || !info->mmap_length ||
        (uint64_t)info->mmap_addr + info->mmap_length > ADDRESS_SPACE_END ||
        kernel_end < kernel_start || kernel_end > PMM_PHYSICAL_LIMIT) return false;
    /* Reserved entries win, even when firmware reports overlapping ranges. */
    if (!map_pass(info, true) || !map_pass(info, false)) return false;
    for (uint32_t page = kernel_start / PAGE_SIZE; page < ((uint64_t)kernel_end + PAGE_SIZE - 1) / PAGE_SIZE; ++page)
        if (!bit(managed, page)) return false;
    for (uint32_t page = 0; page < PAGE_COUNT; ++page) {
        if (bit(managed, page)) {
            ++statistics.usable_pages;
            statistics.limit = (page + 1) * PAGE_SIZE;
        }
    }
    if (!statistics.limit || !reserve(0, 0x100000) ||
        !reserve(kernel_start, kernel_end - kernel_start) || !reserve_boot_data(info)) return false;
    for (uint32_t page = 0; page < statistics.limit / PAGE_SIZE; ++page)
        if (bit(managed, page)) ++statistics.managed_pages;
    statistics.free_pages = statistics.managed_pages;
    next_page = 0x100000 / PAGE_SIZE;
    ready = true;
    return true;
}

uint32_t pmm_allocate_page(void)
{
    uint32_t flags = cpu_interrupt_save();
    uint32_t result = 0;
    if (ready && statistics.free_pages) {
        uint32_t pages = statistics.limit / PAGE_SIZE;
        for (uint32_t scanned = 0; scanned < pages; ++scanned) {
            uint32_t page = next_page;
            next_page = (next_page + 1) % pages;
            if (bit(managed, page) && !bit(allocated, page)) {
                set_bit(allocated, page, true);
                --statistics.free_pages;
                result = page * PAGE_SIZE;
                break;
            }
        }
    }
    cpu_interrupt_restore(flags);
    return result;
}

bool pmm_is_managed(uint32_t physical)
{
    return ready && !(physical % PAGE_SIZE) && physical < statistics.limit && bit(managed, physical / PAGE_SIZE);
}

bool pmm_is_allocated(uint32_t physical)
{
    return pmm_is_managed(physical) && bit(allocated, physical / PAGE_SIZE);
}

bool pmm_free_page(uint32_t physical)
{
    uint32_t flags = cpu_interrupt_save();
    bool valid = pmm_is_allocated(physical);
    if (valid) {
        set_bit(allocated, physical / PAGE_SIZE, false);
        ++statistics.free_pages;
        uint32_t page = physical / PAGE_SIZE;
        if (page < next_page) next_page = page;
    }
    cpu_interrupt_restore(flags);
    return valid;
}

struct pmm_statistics pmm_stats(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct pmm_statistics copy = ready ? statistics : (struct pmm_statistics){0};
    cpu_interrupt_restore(flags);
    return copy;
}
