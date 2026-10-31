#include <stdint.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include "test-files.h"
#include "storage-checks.h"

#define CHECK(condition, name) storage_check((condition), (name))

static void empty_heap(void)
{
    struct heap_statistics stats = heap_stats();
    CHECK(!stats.allocations && !stats.used_bytes && stats.free_bytes < stats.mapped_bytes,
          "all allocations released");
}

static void heap_checks(void)
{
    CHECK(!heap_initialize(), "reject heap reinit");
    CHECK(!kmalloc(0) && !kmalloc(SIZE_MAX) && !kcalloc(0, 1) &&
          !kcalloc(SIZE_MAX, 2) && !kcalloc(1, SIZE_MAX), "zero and overflow");
    CHECK(kfree(NULL) && !kfree((void *)(uintptr_t)HEAP_BASE), "invalid free");
    unsigned char *a = kmalloc(17), *b = kmalloc(33), *c = kcalloc(37, 3);
    CHECK(a && b && c && !((uintptr_t)a % 16) && !((uintptr_t)b % 16) &&
          !((uintptr_t)c % 16), "16 byte alignment");
    for (unsigned i = 0; i < 111; ++i) CHECK(!c[i], "calloc zeroing");
    memset(a, 0x31, 17);
    CHECK(!kfree(a + 1) && !krealloc(a + 1, 10), "interior pointer rejected");
    CHECK(kfree(b) && !kfree(b), "double free rejected");
    CHECK(krealloc(a, 48) == a && a[16] == 0x31, "realloc adjacent free block");
    CHECK(krealloc(a, 1) == a && a[0] == 0x31, "realloc shrink");
    unsigned char *moved = krealloc(a, 3 * 4096 + 7);
    CHECK(moved && moved[0] == 0x31, "realloc moves and grows across pages");
    memset(moved, 0xA7, 3 * 4096 + 7);
    CHECK(!krealloc(moved, SIZE_MAX) && moved[3 * 4096 + 6] == 0xA7,
          "failed realloc preserves original");
    CHECK(!krealloc(moved, 0) && !kfree(moved) && kfree(c), "realloc zero frees");
    empty_heap();

    /* Stress fragmentation while checking every live payload for corruption. */
    unsigned char *slots[32] = {0};
    size_t sizes[32] = {0};
    uint32_t random = 0x12345678;
    for (unsigned step = 0; step < 500; ++step) {
        random = random * 1664525u + 1013904223u;
        unsigned index = (random >> 16) % 32;
        for (unsigned s = 0; s < 32; ++s)
            for (size_t i = 0; i < sizes[s]; ++i)
                CHECK(slots[s][i] == (unsigned char)(s + 1), "fragmentation payload preserved");
        size_t wanted = step % 5 ? (random % 3072) + 1 : 0;
        unsigned char *replacement = krealloc(slots[index], wanted);
        CHECK(!wanted || replacement, "fragmented realloc succeeds");
        for (size_t i = 0; i < (wanted < sizes[index] ? wanted : sizes[index]); ++i)
            CHECK(replacement[i] == index + 1, "realloc contents preserved");
        slots[index] = replacement;
        sizes[index] = wanted;
        if (wanted) memset(replacement, index + 1, wanted);
    }
    for (unsigned i = 0; i < 32; ++i) CHECK(kfree(slots[i]), "fragmented free");
    empty_heap();
    unsigned char *large = kmalloc(HEAP_LIMIT - 64);
    CHECK(large && heap_stats().mapped_bytes == HEAP_LIMIT, "coalesced heap grows to limit");
    large[0] = 0xAB; large[HEAP_LIMIT - 65] = 0xCD;
    CHECK(!kmalloc(64) && !kmalloc(HEAP_LIMIT) && large[0] == 0xAB &&
          large[HEAP_LIMIT - 65] == 0xCD, "virtual limit exhaustion preserves contents");
    CHECK(kfree(large), "free large block");
    empty_heap();
    size_t mapped = heap_stats().mapped_bytes;
    large = kmalloc(heap_stats().free_bytes);
    CHECK(large && !heap_stats().free_bytes && heap_stats().mapped_bytes == mapped,
          "coalesced whole heap reused without growth");
    CHECK(kfree(large), "release whole heap");
}

void storage_checks(void) { heap_checks(); }
