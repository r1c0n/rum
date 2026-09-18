/* Production PMM/paging code, booted in an isolated test kernel. */
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/paging.h>
#include <rum/serial.h>
#include <rum/terminal.h>

extern const char __kernel_start[], __kernel_end[], __text_start[];
const uint32_t paging_readonly_word = 0x13579BDFu;
void kernel_main(uint32_t magic, uint32_t information);
_Noreturn void paging_trigger_null(void);
_Noreturn void paging_trigger_text(void);
_Noreturn void paging_trigger_rodata(void);
_Noreturn void paging_trigger_unmapped(void);
_Noreturn void paging_trigger_readonly(void);

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_paging_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

#if RUM_PAGING_CASE == 0
static uint32_t consume_pages(uint32_t keep)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages > keep) {
        uint32_t page = pmm_allocate_page();
        check(page != 0, "consume pages");
        *(uint32_t *)(uintptr_t)page = head;
        head = page;
    }
    return head;
}

static void release_pages(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "release consumed page");
        head = next;
    }
}
#endif

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    check(pmm_initialize((const void *)(uintptr_t)information,
                         (uintptr_t)__kernel_start, (uintptr_t)__kernel_end), "physical init");
#if RUM_PAGING_CASE == 0
    uint32_t initial = pmm_stats().free_pages;
    uint32_t consumed = consume_pages(2);
    check(!paging_initialize(), "partial page-table OOM");
    check(pmm_stats().free_pages == 2 && !paging_directory_address(), "init rollback");
    release_pages(consumed);
    check(pmm_stats().free_pages == initial, "rollback recovered pages");
#endif
    check(paging_initialize(), "paging init");
    check(!paging_initialize(), "reject reinit");
    uint32_t translated;
    check(!paging_translate(0, &translated), "null guard");
    check(paging_translate((uintptr_t)__kernel_start + 123, &translated) &&
          translated == (uintptr_t)__kernel_start + 123, "kernel identity");
    uint32_t before = pmm_stats().free_pages;
    uint32_t first = pmm_allocate_page(), second = pmm_allocate_page();
    check(first && second && first != second, "unique frames");
    *(volatile uint32_t *)(uintptr_t)first = 0x11223344;
    *(volatile uint32_t *)(uintptr_t)second = 0x55667788;
    check(!paging_map_page(PAGING_DYNAMIC_BASE + 1, first, PAGING_WRITABLE) &&
          !paging_map_page(PAGING_DYNAMIC_BASE, first + 1, PAGING_WRITABLE) &&
          !paging_map_page(PAGING_DYNAMIC_BASE, first, 4) &&
          !paging_map_page(PAGE_SIZE, first, PAGING_WRITABLE) &&
          !paging_map_page(PAGING_DYNAMIC_BASE, (uintptr_t)__kernel_start, PAGING_WRITABLE) &&
          !paging_unmap_page(PAGE_SIZE, NULL), "reject invalid mappings");
    check(paging_map_page(PAGING_DYNAMIC_BASE, first, PAGING_WRITABLE), "map writable alias");
    check(paging_map_page(PAGING_DYNAMIC_BASE + PAGE_SIZE, second, 0), "map readonly alias");
    check(pmm_stats().free_pages == before - 3, "one shared page table");
    volatile uint32_t *alias = (void *)(uintptr_t)PAGING_DYNAMIC_BASE;
    check(alias[0] == 0x11223344, "read alias");
    alias[0] = 0xAABBCCDD;
    check(*(volatile uint32_t *)(uintptr_t)first == 0xAABBCCDD, "write alias");
    check(alias[PAGE_SIZE / 4] == 0x55667788, "readonly alias read");
    check(paging_translate(PAGING_DYNAMIC_BASE + 123, &translated) && translated == first + 123,
          "translate offset");
    check(!paging_map_page(PAGING_DYNAMIC_BASE, second, PAGING_WRITABLE), "no overwrite");
    check(paging_unmap_page(PAGING_DYNAMIC_BASE, &translated) && translated == first,
          "unmap returns frame");
    check(!paging_translate(PAGING_DYNAMIC_BASE, NULL) &&
          !paging_unmap_page(PAGING_DYNAMIC_BASE, NULL), "unmap absent");
    check(pmm_stats().free_pages == before - 3, "retain nonempty table");
    check(paging_map_page(PAGING_DYNAMIC_BASE, second, PAGING_WRITABLE), "remap to another frame");
    check(alias[0] == 0x55667788, "invalidate stale translation");
    check(paging_unmap_page(PAGING_DYNAMIC_BASE, NULL) &&
          paging_unmap_page(PAGING_DYNAMIC_BASE + PAGE_SIZE, NULL), "remove aliases");
    check(pmm_stats().free_pages == before - 2, "reclaim empty page table");
#if RUM_PAGING_CASE == 5
    check(paging_map_page(PAGING_DYNAMIC_BASE, first, 0), "readonly fault setup");
#else
    check(pmm_free_page(first) && pmm_free_page(second) && !pmm_free_page(first), "free data frames");
    check(pmm_stats().free_pages == before, "all frame accounting recovered");
#endif
#if RUM_PAGING_CASE == 0
    consumed = consume_pages(0);
    check(!pmm_allocate_page() && !paging_map_page(PAGING_DYNAMIC_BASE, consumed, PAGING_WRITABLE),
          "runtime table OOM");
    check(!paging_translate(PAGING_DYNAMIC_BASE, NULL), "OOM leaves absent mapping");
    release_pages(consumed);
    check(pmm_stats().free_pages == before, "runtime OOM recovered pages");
    terminal_writestring("rum physical allocator and paging tests passed.\n");
    serial_writestring("rum_paging_test_ok\n");
    cpu_halt();
#elif RUM_PAGING_CASE == 1
    paging_trigger_null();
#elif RUM_PAGING_CASE == 2
    paging_trigger_text();
#elif RUM_PAGING_CASE == 3
    paging_trigger_rodata();
#elif RUM_PAGING_CASE == 4
    paging_trigger_unmapped();
#elif RUM_PAGING_CASE == 5
    paging_trigger_readonly();
#else
#error "Unknown paging case"
#endif
}
