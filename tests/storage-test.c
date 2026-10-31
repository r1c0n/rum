#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <rum/heap.h>
#include "test-files.h"
#include "page-backend.h"
#include "storage-checks.h"

void storage_check(bool condition, const char *name)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); abort(); }
}

int main(void)
{
    assert(!kmalloc(1));
    test_page_budget(1);
    assert(!heap_initialize() && !test_pages_owned() && !heap_stats().mapped_bytes);
    test_page_budget(-1);
    assert(heap_initialize());
    size_t mapped = heap_stats().mapped_bytes;
    unsigned owned = test_pages_owned();
    test_page_budget(2);
    assert(!kmalloc(4 * 4096) && heap_stats().mapped_bytes == mapped && test_pages_owned() == owned);
    test_page_budget(-1);
    unsigned char *original = kmalloc(123);
    assert(original); original[122] = 0xAB;
    test_page_budget(2);
    assert(!krealloc(original, 4 * 4096) && original[122] == 0xAB &&
           heap_stats().mapped_bytes == mapped && test_pages_owned() == owned);
    test_page_budget(-1);
    assert(kfree(original));
    storage_checks();
    puts("PASS: heap alignment/split/coalesce/realloc/calloc/limits/OOM rollback");
    return 0;
}
