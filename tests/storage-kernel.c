/* Exercise the real page allocator, paging, heap and files in ring 0. */
#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/paging.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/terminal.h>
#include "storage-checks.h"

extern const char __kernel_start[], __kernel_end[];
void kernel_main(uint32_t magic, uint32_t information);

void storage_check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_storage_test_failed: "); serial_writestring(name);
        serial_writestring("\n"); cpu_halt();
    }
}

static uint32_t consume(uint32_t keep)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages > keep) {
        uint32_t page = pmm_allocate_page();
        storage_check(page != 0, "consume physical page");
        *(uint32_t *)(uintptr_t)page = head;
        head = page;
    }
    return head;
}

static void release(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        storage_check(pmm_free_page(head), "release physical page"); head = next;
    }
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize();
    storage_check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    storage_check(pmm_initialize((const void *)(uintptr_t)information,
                                 (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
                  paging_initialize(), "physical allocator and paging init");
    uint32_t initial = pmm_stats().free_pages, taken = consume(1);
    storage_check(!heap_initialize() && !heap_stats().mapped_bytes && pmm_stats().free_pages == 1,
                  "heap init table OOM rollback");
    release(taken);
    storage_check(pmm_stats().free_pages == initial && heap_initialize() && ramfs_initialize(),
                  "heap init recovered");
    initial = pmm_stats().free_pages;
    size_t mapped = heap_stats().mapped_bytes;
    unsigned char *original = kmalloc(123);
    storage_check(original != NULL, "original allocation"); original[122] = 0xAB;
    taken = consume(2);
    storage_check(!kmalloc(4 * PAGE_SIZE) && !krealloc(original, 4 * PAGE_SIZE) &&
                  original[122] == 0xAB && heap_stats().mapped_bytes == mapped &&
                  heap_stats().allocations == 1 && pmm_stats().free_pages == 2,
                  "partial growth OOM rollback and realloc preservation");
    release(taken);
    storage_check(kfree(original) && pmm_stats().free_pages == initial, "growth OOM recovered");
    storage_checks();
    storage_check(pmm_stats().free_pages == initial - (HEAP_LIMIT - mapped) / PAGE_SIZE,
                  "heap retained-page physical accounting");
    terminal_writestring("rum heap and RAM filesystem tests passed.\n");
    serial_writestring("rum_storage_test_ok\n");
    cpu_halt();
}
