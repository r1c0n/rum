/* Exercise the production paging registry and shared tables in QEMU. */
#include <rum/cpu.h>
#include <rum/gdt.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/process_limits.h>
#include <rum/serial.h>
#include <rum/timer.h>
#include "paging-spaces.h"

#define FRAME_MASK 0xFFFFF000u
#define PRESENT 1u
#define USER 4u
#define IF 0x200u

extern const char __kernel_start[], __text_start[], __rodata_start[];
extern const char __boot_stack_top[];

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_paging_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static uint32_t flags(void)
{
    uint32_t value;
    __asm__ volatile ("pushfl; popl %0" : "=r"(value));
    return value;
}

static uint32_t *directory(const struct paging_space *space)
{
    return (void *)(uintptr_t)paging_directory_address(space);
}

static void switch_to(struct paging_space *space)
{
    uint32_t previous = flags(), cr3;
    check(paging_switch_space(space), "switch registered context");
    check(((flags() ^ previous) & IF) == 0, "switch preserves IF");
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    check(cr3 == paging_directory_address(space) && paging_active_space() == space,
          "actual CR3 matches active context");
}

static void shared_tables(struct paging_space *space)
{
    uint32_t *kernel = directory(paging_kernel_space()), *entries = directory(space);
    check(entries && entries != kernel, "distinct directory frame");
    for (uint32_t i = 0; i < RUM_USER_BASE / RUM_PAGE_TABLE_SPAN; ++i) {
        /* Accessed bits in directory entries are maintained independently. */
        check((entries[i] & ~0x20u) == (kernel[i] & ~0x20u), "borrow kernel tables");
        if (entries[i] & PRESENT) {
            check(!(entries[i] & USER), "supervisor kernel directory entry");
            const uint32_t *table = (void *)(uintptr_t)(entries[i] & FRAME_MASK);
            for (uint32_t j = 0; j < 1024; ++j)
                check(!(table[j] & PRESENT) || !(table[j] & USER), "supervisor kernel page");
        }
    }
    for (uint32_t i = RUM_USER_BASE / RUM_PAGE_TABLE_SPAN; i < 1024; ++i)
        check(entries[i] == 0, "no private user/unassigned mappings");
    check(!paging_translate(space, 0, NULL), "context null guard");
    const uint32_t readonly[] = {(uintptr_t)__text_start, (uintptr_t)__rodata_start};
    for (size_t i = 0; i < sizeof readonly / sizeof readonly[0]; ++i) {
        const uint32_t *table = (void *)(uintptr_t)(entries[readonly[i] >> 22] & FRAME_MASK);
        check(!(table[(readonly[i] >> 12) & 1023] & PAGING_WRITABLE), "context kernel protection");
    }
    uint32_t physical;
    const uintptr_t writable_cpu[] = {(uintptr_t)rum_gdt, (uintptr_t)&rum_tss};
    for (size_t i = 0; i < sizeof writable_cpu / sizeof writable_cpu[0]; ++i) {
        const uint32_t *table = (void *)(uintptr_t)(entries[writable_cpu[i] >> 22] & FRAME_MASK);
        check(table[(writable_cpu[i] >> 12) & 1023] & PAGING_WRITABLE, "shared writable GDT/TSS");
    }
    check(paging_translate(space, (uintptr_t)__kernel_start, &physical) &&
          physical == (uintptr_t)__kernel_start, "shared kernel identity");
}

static uint32_t consume_pages(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) {
        *(uint32_t *)(uintptr_t)page = head;
        head = page;
    }
    return head;
}

static void release_pages(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "return consumed frame");
        head = next;
    }
}

void paging_space_checks(void)
{
    struct paging_space *kernel = paging_kernel_space();
    struct paging_space *invalid = (void *)(uintptr_t)0x12345;
    check(!paging_space_create(), "creation requires initialized heap");
    check(heap_initialize(), "context test heap init");
    check(!paging_space_destroy(kernel) && !paging_space_destroy(NULL) &&
          !paging_space_destroy(invalid) && !paging_switch_space(invalid) &&
          !paging_directory_address(invalid) && !paging_translate(invalid, 0, NULL),
          "reject unregistered/kernel handles without dereference");
    uint32_t before = pmm_stats().free_pages;
    size_t allocations = heap_stats().allocations, used = heap_stats().used_bytes;
    struct paging_space *first = paging_space_create(), *second = paging_space_create();
    check(first && second && first != second && pmm_stats().free_pages == before - 2,
          "two independently owned directory frames");
    shared_tables(first);
    shared_tables(second);
    switch_to(first);
    check(!paging_space_destroy(first) && !paging_unmap_page(second, HEAP_BASE, NULL),
          "refuse active destruction and foreign kernel mutation");

    /* Grow existing shared heap tables after both directories already exist. */
    unsigned char *payload = kmalloc(3 * PAGE_SIZE);
    check(payload != NULL, "heap growth in non-kernel context");
    payload[0] = 0x12;
    payload[3 * PAGE_SIZE - 1] = 0x34;
    switch_to(second);
    check(payload[0] == 0x12 && payload[3 * PAGE_SIZE - 1] == 0x34, "late heap pages shared");
    payload[3 * PAGE_SIZE - 1] = 0x56;
    switch_to(first);
    check(payload[3 * PAGE_SIZE - 1] == 0x56, "heap writes visible across contexts");

    /* A brand-new kernel PDE must also reach already-created directories. */
    uint32_t frame = pmm_allocate_page(), other = pmm_allocate_page();
    check(frame && other, "alias data frames");
    *(uint32_t *)(uintptr_t)frame = 0x11223344;
    *(uint32_t *)(uintptr_t)other = 0x55667788;
    uint32_t alias = RUM_KERNEL_ALIAS_BASE, physical;
    uint32_t count = pmm_stats().free_pages;
    check(!paging_map_page(second, alias, frame, PAGING_WRITABLE) &&
          paging_map_page(kernel, alias, frame, PAGING_WRITABLE), "kernel owns new shared table");
    check(pmm_stats().free_pages == count - 1, "new shared table counted once");
    check(*(volatile uint32_t *)(uintptr_t)alias == 0x11223344, "late table visible in active context");
    switch_to(second);
    check(paging_translate(second, alias + 7, &physical) && physical == frame + 7 &&
          *(volatile uint32_t *)(uintptr_t)alias == 0x11223344, "late table in second context");
    check(paging_map_page(kernel, alias + PAGE_SIZE, other, 0) &&
          paging_unmap_page(kernel, alias, &physical) && physical == frame &&
          pmm_is_allocated(frame), "unmap retains caller data frame");
    check(paging_map_page(kernel, alias, other, PAGING_WRITABLE) &&
          *(volatile uint32_t *)(uintptr_t)alias == 0x55667788, "active remap invalidates TLB");
    switch_to(first);
    check(*(volatile uint32_t *)(uintptr_t)alias == 0x55667788, "inactive remap flushes on switch");
    check(paging_unmap_page(kernel, alias, NULL) && paging_unmap_page(kernel, alias + PAGE_SIZE, NULL),
          "retire shared table while child active");
    check(!directory(kernel)[alias >> 22] && !directory(first)[alias >> 22] &&
          !directory(second)[alias >> 22] && pmm_stats().free_pages == count,
          "detach table everywhere before reclaiming its frame");
    check(!paging_translate(first, alias, NULL) && !paging_translate(second, alias, NULL),
          "no stale retired table references");
    check(paging_map_page(kernel, alias, frame, PAGING_WRITABLE) &&
          *(volatile uint32_t *)(uintptr_t)alias == 0x11223344, "recreate retired shared table");
    shared_tables(first);
    shared_tables(second);

    /* IRQ handlers, CPU tables, and the boot stack remain valid under both CR3s. */
    pic_initialize();
    timer_initialize();
    pic_unmask(0);
    cpu_interrupt_enable();
    for (unsigned i = 0; i < 2; ++i) {
        switch_to(i ? second : first);
        volatile uint8_t *access = &rum_gdt[TSS_SELECTOR >> 3].access;
        *access = *access; /* Real write with CR0.WP set under a child CR3. */
        uint32_t previous = flags();
        check(gdt_set_kernel_stack((uintptr_t)__boot_stack_top) &&
              rum_tss.esp0 == (uintptr_t)__boot_stack_top && ((flags() ^ previous) & IF) == 0,
              "writable shared TSS and stack update preserves IF");
        uint32_t start = timer_ticks();
        while (timer_ticks() == start) __asm__ volatile ("hlt" : : : "memory");
    }
    (void)cpu_interrupt_save();
    switch_to(kernel);

    /* Refuse untracked private entries; never guess ownership from a raw PDE. */
    directory(first)[RUM_USER_BASE >> 22] = directory(kernel)[0];
    check(!paging_space_destroy(first), "reject unsupported private table ownership");
    directory(first)[RUM_USER_BASE >> 22] = 0;
    count = pmm_stats().free_pages;
    check(paging_space_destroy(first) && pmm_stats().free_pages == count + 1 &&
          !paging_space_destroy(first) && !paging_switch_space(first) &&
          !paging_directory_address(first), "destroy inactive directory once");
    switch_to(second);
    check(*(volatile uint32_t *)(uintptr_t)alias == 0x11223344 && payload[0] == 0x12,
          "other context survives destruction");
    switch_to(kernel);
    check(paging_space_destroy(second) && paging_unmap_page(kernel, alias, NULL) &&
          pmm_free_page(frame) && pmm_free_page(other) && kfree(payload), "release owned test resources");
    check(heap_stats().allocations == allocations && heap_stats().used_bytes == used,
          "context metadata and payload reclaimed");

    /* The configured bound must fail before allocating metadata or a frame. */
    struct paging_space *spaces[RUM_PROCESS_LIMIT];
    before = pmm_stats().free_pages;
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) {
        spaces[i] = paging_space_create();
        check(spaces[i] != NULL, "create up to directory limit");
    }
    count = pmm_stats().free_pages;
    size_t limit_allocations = heap_stats().allocations;
    check(count == before - RUM_PROCESS_LIMIT && !paging_space_create() &&
          pmm_stats().free_pages == count && heap_stats().allocations == limit_allocations,
          "directory limit has no side effects");
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i)
        check(paging_space_destroy(spaces[i]), "return bounded directories");
    check(pmm_stats().free_pages == before && heap_stats().allocations == allocations,
          "bounded creation/destruction leaks no frames or metadata");

    uint32_t taken = consume_pages();
    check(!paging_space_create() && !pmm_stats().free_pages &&
          heap_stats().allocations == allocations && heap_stats().used_bytes == used &&
          paging_active_space() == kernel, "directory allocation OOM rollback");
    release_pages(taken);
    check(pmm_stats().free_pages == before, "OOM recovery frame count");

    void *fill = kmalloc(HEAP_LIMIT - 2 * HEAP_ALIGNMENT);
    check(fill != NULL, "fill metadata heap");
    count = pmm_stats().free_pages;
    check(!paging_space_create() && pmm_stats().free_pages == count &&
          heap_stats().allocations == allocations + 1, "metadata OOM allocates no directory");
    check(kfree(fill), "release metadata heap fill");
    for (unsigned i = 0; i < 32; ++i) {
        struct paging_space *space = paging_space_create();
        check(space && paging_space_destroy(space), "repeat create/destroy after OOM");
    }
    check(pmm_stats().free_pages == count && heap_stats().allocations == allocations &&
          heap_stats().used_bytes == used, "repeated lifecycle leaks no owned resources");
    user_address_space_checks();
    serial_writestring("rum_paging_spaces_ok\n");
}
