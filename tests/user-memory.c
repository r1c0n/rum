/* Production user-mapping ownership, validation, copy and rollback checks. */
#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/process_limits.h>
#include <rum/serial.h>
#include "paging-spaces.h"

#define PROGRAM_ADDRESS (RUM_USER_BASE + RUM_PAGE_TABLE_SPAN - RUM_PAGE_SIZE)
#define PROGRAM_PAGES 2u
#define STACK_PAGES (RUM_USER_STACK_SIZE / RUM_PAGE_SIZE)
#define USER_PAGE_LIMIT (RUM_PROCESS_USER_BYTES / RUM_PAGE_SIZE)

volatile uint32_t paging_user_test_ready;
uint32_t paging_user_test_directories[2];
uint32_t paging_user_test_program = PROGRAM_ADDRESS;
uint32_t paging_user_test_stack = RUM_USER_STACK_BASE;
struct paging_statistics paging_user_test_stats;
struct pmm_statistics paging_user_test_physical;

static unsigned char source[96], copied[96];

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_paging_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static void zeroed_pages(const struct paging_space *space, uint32_t address, uint32_t pages)
{
    for (uint32_t page = 0; page < pages; ++page) {
        uint32_t physical;
        check(paging_translate(space, address + page * PAGE_SIZE, &physical), "zero-page translation");
        const unsigned char *bytes = (const void *)(uintptr_t)physical;
        for (uint32_t i = 0; i < PAGE_SIZE; ++i)
            check(bytes[i] == 0, "user frame zero before exposure");
    }
}

static uint32_t consume_pages(uint32_t keep)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages > keep) {
        uint32_t page = pmm_allocate_page();
        check(page != 0, "consume user-test pages");
        *(uint32_t *)(uintptr_t)page = head;
        head = page;
    }
    return head;
}

static void release_pages(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "release user-test pages");
        head = next;
    }
}

static void allocation_failures(void)
{
    for (uint32_t keep = 0; keep < 4; ++keep) {
        struct paging_space *space = paging_space_create();
        check(space != NULL, "failure-space directory");
        uint32_t owned_free = pmm_stats().free_pages;
        struct paging_statistics before = paging_stats();
        uint32_t held = consume_pages(keep);
        check(!paging_user_allocate(space, PROGRAM_ADDRESS, PROGRAM_PAGES, PAGING_WRITABLE) &&
              pmm_stats().free_pages == keep && !paging_user_page_count(space) &&
              !paging_translate(space, PROGRAM_ADDRESS, NULL), "transactional user allocation failure");
        struct paging_statistics after = paging_stats();
        check(after.private_table_pages == before.private_table_pages &&
              after.user_pages == before.user_pages, "failure returns private tables and pages");
        release_pages(held);
        check(pmm_stats().free_pages == owned_free && paging_space_destroy(space),
              "failure-space teardown");
    }

    struct paging_space *space = paging_space_create();
    check(space && paging_user_allocate(space, RUM_USER_BASE, 1, PAGING_WRITABLE),
          "existing mapping before OOM");
    const uint32_t marker = 0x726f6c6cu;
    check(paging_copy_to_user(space, RUM_USER_BASE, &marker, sizeof marker), "seed retained mapping");
    uint32_t pages = paging_user_page_count(space), held = consume_pages(0), actual = 0;
    check(!paging_user_allocate(space, RUM_USER_BASE + PAGE_SIZE, 1, PAGING_WRITABLE) &&
          paging_user_page_count(space) == pages &&
          paging_copy_from_user(space, &actual, RUM_USER_BASE, sizeof actual) && actual == marker,
          "failed extension preserves existing mapping");
    release_pages(held);
    check(paging_space_destroy(space), "retained mapping automatic teardown");
}

static void limits_and_teardown(void)
{
    struct paging_space *space = paging_space_create();
    check(space != NULL, "limit-space directory");
    uint32_t before = pmm_stats().free_pages;
    struct paging_statistics stats = paging_stats();
    check(!paging_user_allocate(space, RUM_USER_BASE, USER_PAGE_LIMIT + 1, PAGING_WRITABLE) &&
          pmm_stats().free_pages == before && !paging_user_page_count(space),
          "reject request above process page limit");
    if (before > USER_PAGE_LIMIT + 8) {
        check(paging_user_allocate(space, RUM_USER_BASE, USER_PAGE_LIMIT, PAGING_WRITABLE) &&
              paging_user_page_count(space) == USER_PAGE_LIMIT,
              "map exact process page limit");
        check(!paging_user_allocate(space, RUM_USER_STACK_BASE, 1, PAGING_WRITABLE) &&
              paging_user_page_count(space) == USER_PAGE_LIMIT,
              "page limit rejects one more stack page");
    }
    check(paging_space_destroy(space), "limit-space automatic teardown");
    struct paging_statistics restored = paging_stats();
    check(restored.spaces == stats.spaces - 1 &&
          restored.private_table_pages == stats.private_table_pages &&
          restored.user_pages == stats.user_pages, "limit teardown restores paging ownership");

    before = pmm_stats().free_pages;
    space = paging_space_create();
    check(space && paging_user_allocate(space, PROGRAM_ADDRESS, PROGRAM_PAGES, PAGING_WRITABLE) &&
          paging_user_allocate(space, RUM_USER_STACK_BASE, STACK_PAGES, PAGING_WRITABLE),
          "automatic teardown owners");
    check(paging_space_destroy(space) && pmm_stats().free_pages == before,
          "destroy releases user data, tables, directory and metadata");
}

static void invalid_requests(struct paging_space *space)
{
    struct paging_space *kernel = paging_kernel_space();
    struct paging_space *invalid = (void *)(uintptr_t)0x12345;
    uint32_t free = pmm_stats().free_pages;
    check(!paging_user_allocate(NULL, RUM_USER_BASE, 1, 0) &&
          !paging_user_allocate(kernel, RUM_USER_BASE, 1, 0) &&
          !paging_user_allocate(invalid, RUM_USER_BASE, 1, 0) &&
          !paging_user_allocate(space, RUM_USER_BASE + 1, 1, 0) &&
          !paging_user_allocate(space, RUM_USER_BASE, 0, 0) &&
          !paging_user_allocate(space, RUM_USER_BASE, 1, 0x80) &&
          !paging_user_allocate(space, RUM_USER_PROGRAM_END - PAGE_SIZE, 2, 0) &&
          !paging_user_allocate(space, RUM_USER_STACK_GUARD_BASE, 1, 0) &&
          !paging_user_allocate(space, RUM_USER_END, 1, 0) &&
          !paging_user_allocate(space, 0xFFFFF000u, 2, 0), "reject invalid user mappings");
    check(!paging_user_protect(space, RUM_USER_BASE, 1, 0) &&
          !paging_user_release(space, RUM_USER_BASE, 1) &&
          !paging_user_page_count(kernel) && !paging_user_page_count(invalid) &&
          pmm_stats().free_pages == free, "invalid user operations have no side effects");
    check(paging_user_accessible(space, 0, 0, true) &&
          paging_copy_from_user(space, NULL, 0, 0) &&
          paging_copy_to_user(space, 0, NULL, 0), "zero-byte copies inspect no address");
}

void user_address_space_checks(void)
{
    allocation_failures();
    limits_and_teardown();

    struct paging_space *kernel = paging_kernel_space();
    struct paging_space *first = paging_space_create(), *second = paging_space_create();
    check(first && second, "two isolated user spaces");
    invalid_requests(first);
    check(paging_user_allocate(first, PROGRAM_ADDRESS, PROGRAM_PAGES, PAGING_WRITABLE) &&
          paging_user_allocate(second, PROGRAM_ADDRESS, PROGRAM_PAGES, PAGING_WRITABLE),
          "same virtual program pages in two spaces");
    zeroed_pages(first, PROGRAM_ADDRESS, PROGRAM_PAGES);
    zeroed_pages(second, PROGRAM_ADDRESS, PROGRAM_PAGES);
    check(paging_user_allocate(first, RUM_USER_STACK_BASE, STACK_PAGES, PAGING_WRITABLE) &&
          paging_user_allocate(second, RUM_USER_STACK_BASE, STACK_PAGES, PAGING_WRITABLE),
          "private fixed user stacks");
    zeroed_pages(first, RUM_USER_STACK_BASE, STACK_PAGES);
    zeroed_pages(second, RUM_USER_STACK_BASE, STACK_PAGES);
    check(!paging_translate(first, 0, NULL) &&
          !paging_translate(first, RUM_USER_STACK_GUARD_BASE, NULL) &&
          !paging_user_accessible(first, RUM_USER_STACK_GUARD_BASE, 1, false),
          "null and stack guard remain unmapped");

    for (uint32_t i = 0; i < sizeof source; ++i) source[i] = (unsigned char)(i + 1);
    uint32_t cross = PROGRAM_ADDRESS + PAGE_SIZE - 17;
    check(paging_copy_to_user(first, cross, source, sizeof source) &&
          paging_copy_from_user(first, copied, cross, sizeof copied) &&
          !memcmp(source, copied, sizeof source), "checked cross-page copy round trip");
    memset(source, 0xA5, sizeof source);
    check(paging_copy_to_user(second, cross, source, sizeof source), "seed isolated address space");
    check(paging_switch_space(first) && *(volatile unsigned char *)(uintptr_t)cross == 1 &&
          paging_switch_space(second) && *(volatile unsigned char *)(uintptr_t)cross == 0xA5 &&
          paging_switch_space(kernel), "same user address has distinct physical data");

    static const char island[] = "island";
    char text[16] = "unchanged";
    size_t length = 99;
    uint32_t string = PROGRAM_ADDRESS + PAGE_SIZE - 2;
    check(paging_copy_to_user(first, string, island, sizeof island) &&
          paging_copy_string_from_user(first, text, sizeof text, string, &length) &&
          length == sizeof island - 1 && !memcmp(text, island, sizeof island),
          "bounded string copy across pages");
    static const char no_nul[] = {'r', 'u', 'm', '!'};
    check(paging_copy_to_user(first, PROGRAM_ADDRESS + 64, no_nul, sizeof no_nul), "seed unterminated text");
    memcpy(text, "unchanged", 10); length = 99;
    check(!paging_copy_string_from_user(first, text, sizeof no_nul, PROGRAM_ADDRESS + 64, &length) &&
          !memcmp(text, "unchanged", 10) && length == 99, "failed string copy changes no outputs");

    check(paging_user_protect(first, PROGRAM_ADDRESS + PAGE_SIZE, 1, 0) &&
          paging_user_accessible(first, cross, sizeof source, false) &&
          !paging_user_accessible(first, cross, sizeof source, true), "independent read-only protection");
    unsigned char retained[sizeof source];
    check(paging_copy_from_user(first, retained, cross, sizeof retained) &&
          !paging_copy_to_user(first, cross, source, sizeof source) &&
          paging_copy_from_user(first, copied, cross, sizeof copied) &&
          !memcmp(copied, retained, sizeof copied), "write rejection preserves full range");
    check(!paging_user_protect(first, PROGRAM_ADDRESS, 3, 0) &&
          paging_user_accessible(first, PROGRAM_ADDRESS, 1, true) &&
          !paging_user_release(first, PROGRAM_ADDRESS, 3) &&
          paging_user_page_count(first) == PROGRAM_PAGES + STACK_PAGES,
          "protect/release validate complete range before mutation");
    check(paging_user_protect(first, PROGRAM_ADDRESS + PAGE_SIZE, 1, PAGING_WRITABLE),
          "restore writable program page");

    memset(copied, 0xCC, sizeof copied);
    check(!paging_copy_from_user(first, copied, RUM_USER_BASE, sizeof copied) && copied[0] == 0xCC &&
          !paging_copy_to_user(first, RUM_USER_BASE, source, sizeof source),
          "hole validation happens before copying");
    unsigned char last = 0x5A;
    check(paging_copy_to_user(first, RUM_USER_END - 1, &last, 1) &&
          !paging_copy_to_user(first, RUM_USER_END - 1, &last, 2), "last user byte and overflow boundary");

    struct paging_statistics stats = paging_stats();
    check(stats.spaces == 3 && stats.directory_pages == 3 &&
          stats.private_table_pages == 6 && stats.user_pages == 2 * (PROGRAM_PAGES + STACK_PAGES),
          "user page/table ownership statistics");
    paging_user_test_directories[0] = paging_directory_address(first);
    paging_user_test_directories[1] = paging_directory_address(second);
    paging_user_test_stats = stats;
    paging_user_test_physical = pmm_stats();
    paging_user_test_ready = 1;
    serial_writestring("rum_user_memory_ok\n");
}
