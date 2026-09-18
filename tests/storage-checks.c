#include <stdint.h>
#include <rum/embedded.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/ramfs.h>
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

struct listing { char names[RAMFS_MAX_FILES][RAMFS_NAME_CAPACITY]; size_t count, bytes; };
static bool collect(const char *name, size_t size, void *context)
{
    struct listing *listing = context;
    CHECK(listing->count < RAMFS_MAX_FILES, "listing bounded");
    size_t i = 0;
    do { listing->names[listing->count][i] = name[i]; } while (name[i++]);
    ++listing->count;
    listing->bytes += size;
    return true;
}

static void remove_all(void)
{
    struct listing listing = {0};
    ramfs_list(collect, &listing);
    CHECK(listing.count == ramfs_stats().files && listing.bytes == ramfs_stats().bytes,
          "filesystem listing and totals");
    for (size_t i = 0; i < listing.count; ++i)
        CHECK(ramfs_remove(listing.names[i]), "remove listed file");
}

static bool stop_after_one(const char *name, size_t size, void *context)
{
    (void)name; (void)size;
    ++*(unsigned *)context;
    return false;
}

static void filesystem_checks(void)
{
    CHECK(!ramfs_initialize(), "reject filesystem reinit");
    const unsigned char binary[] = { 0, 'r', 0xFF, 'u', 'm', '\n' };
    CHECK(ramfs_put("binary", binary, sizeof binary), "create binary file");
    const unsigned char *data = NULL;
    size_t size = 0;
    CHECK(ramfs_read("/binary", &data, &size) && size == sizeof binary &&
          !memcmp(data, binary, size), "binary file exact bytes");
    CHECK(ramfs_put("binary", data + 1, size - 1), "replace from borrowed bytes");
    CHECK(ramfs_read("binary", &data, &size) && size == sizeof binary - 1 &&
          !memcmp(data, binary + 1, size), "borrowed replacement copied before free");
    CHECK(!ramfs_put("binary", NULL, 1) && !ramfs_put("binary", binary, RAMFS_FILE_LIMIT + 1) &&
          ramfs_read("binary", &data, &size) && size == 5 && data[0] == 'r',
          "invalid writes preserve original");
    CHECK(ramfs_put("empty", NULL, 0) && ramfs_read("empty", &data, &size) && !size,
          "empty files");
    for (const char **name = (const char *[]){ "", "/", ".", "..", "a/b", "//a", "a b", NULL }; *name; ++name)
        CHECK(!ramfs_put(*name, NULL, 0) && !ramfs_read(*name, NULL, NULL) &&
              !ramfs_remove(*name), "invalid path rejected");
    char name[65];
    memset(name, 'a', 63); name[63] = 0;
    CHECK(ramfs_put(name, NULL, 0), "63 character name");
    name[63] = 'a'; name[64] = 0;
    CHECK(!ramfs_put(name, NULL, 0), "64 character name rejected");
    remove_all(); empty_heap();
    for (unsigned i = 0; i < RAMFS_MAX_FILES; ++i) {
        char numbered[] = "file00";
        numbered[4] = '0' + i / 10; numbered[5] = '0' + i % 10;
        CHECK(ramfs_put(numbered, binary, i % sizeof binary), "fill file table");
    }
    CHECK(!ramfs_put("overflow", NULL, 0) && ramfs_put("file00", binary, sizeof binary),
          "file limit permits replacement");
    unsigned visits = 0;
    ramfs_list(stop_after_one, &visits);
    CHECK(visits == 1, "listing can stop early");
    CHECK(ramfs_remove("file00") && ramfs_remove("file31") && ramfs_remove("file63") &&
          ramfs_put("reuse", NULL, 0) && !ramfs_remove("file63"), "unlink head middle tail");
    remove_all(); empty_heap();
    unsigned char *large = kmalloc(RAMFS_FILE_LIMIT);
    CHECK(large != NULL, "large file input");
    for (size_t i = 0; i < RAMFS_FILE_LIMIT; ++i) large[i] = (unsigned char)i;
    CHECK(ramfs_put("large", large, RAMFS_FILE_LIMIT) && ramfs_read("large", &data, &size) &&
          size == RAMFS_FILE_LIMIT && !memcmp(data, large, size), "64 KiB binary file");
    CHECK(kfree(large) && ramfs_remove("large"), "release large file");

    CHECK(ramfs_put("safe", binary, sizeof binary), "OOM original file");
    void *guard = kmalloc(heap_stats().free_bytes);
    CHECK(guard && !ramfs_put("safe", binary, sizeof binary) && !ramfs_put("new", binary, 1),
          "failed create and replace");
    CHECK(ramfs_stats().files == 1 && ramfs_stats().bytes == sizeof binary &&
          ramfs_read("safe", &data, &size) && !memcmp(data, binary, size), "OOM leaves filesystem intact");
    CHECK(kfree(guard) && ramfs_remove("safe"), "recover filesystem OOM");
    empty_heap();
    if (embedded_file_count) {
        guard = kmalloc(heap_stats().free_bytes);
        CHECK(guard && !embedded_files_install() && !ramfs_stats().files,
              "embedded install OOM rolls back");
        CHECK(kfree(guard), "release embedded OOM guard");
    }
    if (embedded_file_count > 1) {
        const struct embedded_file *first = &embedded_files[0];
        size_t available = heap_stats().free_bytes;
        CHECK(ramfs_put(first->name, first->data, first->size), "measure first embedded file");
        size_t cost = available - heap_stats().free_bytes;
        CHECK(ramfs_remove(first->name), "remove measured file");
        guard = kmalloc(available - cost - 64);
        CHECK(guard && !embedded_files_install() && !ramfs_stats().files &&
              heap_stats().allocations == 1, "partial embedded install rolls back nodes and data");
        CHECK(kfree(guard), "release partial-install guard");
    }
    CHECK(embedded_files_install() && ramfs_stats().files == embedded_file_count,
          "embedded files load after recovery");
    for (size_t i = 0; i < embedded_file_count; ++i)
        CHECK(ramfs_read(embedded_files[i].name, &data, &size) && size == embedded_files[i].size &&
              (!size || !memcmp(data, embedded_files[i].data, size)), "embedded file exact bytes");
    if (embedded_file_count)
        CHECK(!embedded_files_install(), "embedded install does not overwrite existing files");
    remove_all(); empty_heap();
    CHECK(embedded_files_install(), "restore boot files");
}

void storage_checks(void)
{
    heap_checks();
    filesystem_checks();
}
