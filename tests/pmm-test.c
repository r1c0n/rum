#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <rum/pmm.h>

static struct multiboot_info *const info = (void *)0x500000;
static uint8_t *const map = (void *)0x502000;

static void reset(void)
{
    memset(info, 0, 0x10000);
    info->flags = MULTIBOOT_MEMORY_MAP;
    info->mmap_addr = (uintptr_t)map;
}

static void entry(uint64_t address, uint64_t length, uint32_t type, uint32_t extra)
{
    struct multiboot_mmap_entry item = {20 + extra, address, length, type};
    memcpy(map + info->mmap_length, &item, sizeof item);
    info->mmap_length += sizeof item + extra;
}

static void assert_unready(void)
{
    assert(pmm_allocate_page() == 0);
    assert(pmm_stats().free_pages == 0);
    assert(!pmm_free_page(0x100000));
}

int main(void)
{
    void *mapping = mmap(info, 0x10000, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapping != MAP_FAILED);
    reset();
    /* Unsorted map, extensions, partial reserved pages, kernel/BSS/boot data. */
    entry(0x180001, 1, 2, 8);
    entry(0, 0x800000, 1, 0);
    assert(pmm_initialize(info, 0x200000, 0x202010));
    assert(pmm_stats().usable_pages == 2047);
    assert(pmm_stats().managed_pages == 1792 - 1 - 3 - 2);
    assert(pmm_stats().limit == 0x800000);
    for (uint32_t page = 0; page < 0x100000; page += PAGE_SIZE)
        assert(!pmm_is_managed(page));
    assert(!pmm_is_managed(0x180000));
    assert(!pmm_is_managed(0x202000));
    assert(!pmm_is_managed(0x500000));
    assert(!pmm_is_managed(0x502000));
    assert(pmm_is_managed(0x503000));
    /* Exhaustion returns zero; every returned frame is unique, aligned RAM. */
    uint32_t pages[2048], count = 0, physical;
    while ((physical = pmm_allocate_page())) {
        assert(count < 2048);
        assert(pmm_is_allocated(physical) && !(physical % PAGE_SIZE));
        if (count) assert(physical > pages[count - 1]);
        pages[count++] = physical;
    }
    assert(count == pmm_stats().managed_pages && pmm_stats().free_pages == 0);
    assert(!pmm_free_page(0) && !pmm_free_page(0x200000));
    assert(!pmm_free_page(pages[0] + 1));
    assert(pmm_free_page(pages[5]) && !pmm_free_page(pages[5]));
    assert(pmm_allocate_page() == pages[5]);
    for (uint32_t i = 0; i < count; ++i) assert(pmm_free_page(pages[i]));
    assert(pmm_stats().free_pages == count);

    reset();
    entry(0x100001, PAGE_SIZE * 3, 1, 0);
    assert(pmm_initialize(info, 0, 0));
    assert(pmm_stats().free_pages == 2); /* Only wholly covered pages. */
    assert(pmm_allocate_page() == 0x101000);
    assert(pmm_allocate_page() == 0x102000);
    assert(pmm_allocate_page() == 0);
    reset();
    entry(PMM_PHYSICAL_LIMIT - PAGE_SIZE * 2, PAGE_SIZE * 3, 1, 0);
    entry(1ull << 32, 0x100000, 1, 0); /* High RAM does not wrap to low addresses. */
    assert(pmm_initialize(info, 0, 0));
    assert(pmm_stats().free_pages == 2 && pmm_stats().limit == PMM_PHYSICAL_LIMIT);
    assert(pmm_allocate_page() == PMM_PHYSICAL_LIMIT - PAGE_SIZE * 2);
    assert(pmm_allocate_page() == PMM_PHYSICAL_LIMIT - PAGE_SIZE);
    assert(pmm_allocate_page() == 0 && !pmm_free_page(PMM_PHYSICAL_LIMIT));

    reset();
    entry(0x100000, 0x700000, 1, 0);
    info->flags |= (1u << 2) | (1u << 3) | (1u << 5) | (1u << 9) | (1u << 12);
    info->cmdline = 0x506FFF;
    strcpy((char *)0x506FFF, "rum"); /* Crossing a page boundary. */
    info->mods_addr = 0x503000;
    info->mods_count = 1;
    struct multiboot_module *module = (void *)0x503000;
    *module = (struct multiboot_module){0x504001, 0x505001, 0x508000, 0};
    strcpy((char *)0x508000, "module");
    info->boot_loader_name = 0x509000;
    strcpy((char *)0x509000, "loader");
    info->symbols[0] = 1; info->symbols[1] = 40; info->symbols[2] = 0x50A000;
    uint32_t *section = (void *)0x50A000;
    section[3] = 0x50B001; section[5] = PAGE_SIZE;
    info->framebuffer_addr = 0x50D001;
    info->framebuffer_pitch = PAGE_SIZE; info->framebuffer_height = 1;
    info->framebuffer_type = 2;
    assert(pmm_initialize(info, 0, 0));
    for (uint32_t address = 0x503000; address < 0x50F000; address += PAGE_SIZE)
        assert(!pmm_is_managed(address));

    /* Invalid inputs fail closed, even after an earlier successful init. */
    info->flags = 0;
    assert(!pmm_initialize(info, 0, 0)); assert_unready();
    reset();
    entry(0x100000, 0x100000, 1, 0);
    map[0] = 19;
    assert(!pmm_initialize(info, 0, 0)); assert_unready();
    reset();
    entry(0x100000, 0x100000, 1, 8);
    --info->mmap_length;
    assert(!pmm_initialize(info, 0, 0)); assert_unready();
    reset();
    entry(UINT64_MAX - 1, 8, 1, 0);
    assert(!pmm_initialize(info, 0, 0)); assert_unready();
    reset();
    entry(0x100000, PAGE_SIZE, 1, 0);
    assert(!pmm_initialize(info, 0x200000, 0x201000)); assert_unready();
    reset();
    entry(0x100000, 0x700000, 1, 0);
    info->flags |= 1u << 3; info->mods_addr = 0xFFFFFFF0; info->mods_count = 2;
    assert(!pmm_initialize(info, 0, 0)); assert_unready();
    reset();
    entry(0x100000, 0x700000, 1, 0);
    info->flags |= 1u << 2; info->cmdline = 0x503000;
    memset((void *)0x503000, 'x', PAGE_SIZE);
    assert(!pmm_initialize(info, 0, 0)); assert_unready();
    assert(munmap(mapping, 0x10000) == 0);
    puts("PASS: physical pages, map extensions/overlap/rounding, reservations, exhaustion/reuse, invalid frees/maps, 1 GiB cap");
    return 0;
}
