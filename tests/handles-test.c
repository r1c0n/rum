#include <assert.h>
#include <stdio.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include "fs-backend.h"
#include "page-backend.h"
#include "test-files.h"

uint32_t handle_test_flags = 0x200u;
static bool in_irq;
bool irq_in_handler(void) { return in_irq; }
static void empty(void)
{
    struct process_handle_statistics s = process_handles_stats();
    assert(!s.descriptors && !s.descriptions && !s.aliases);
}
static void aliases(void)
{
    struct process_handles a = {0}, child = {0};
    assert(!process_handles_initialize(&a));
    assert(process_handle_open(&a, NULL, "/DATA.BIN", FS_READ | FS_WRITE, 0) == 3);
    assert(process_handle_open(&a, NULL, "/DATA.BIN", FS_READ, 0) == 4);
    unsigned refs = fs_stats().references;
    assert(process_handle_duplicate(&a, 3, RUM_DUP_ANY, 0) == 5 && fs_stats().references == refs);
    char bytes[8] = {0}; uint64_t position;
    assert(process_handle_read(process_handle_get(&a, 3), bytes, 2) == 2 && bytes[0] == 'a');
    assert(process_handle_read(process_handle_get(&a, 5), bytes, 2) == 2 && bytes[0] == 'c');
    assert(process_handle_read(process_handle_get(&a, 4), bytes, 1) == 1 && bytes[0] == 'a');
    assert(!process_handle_seek(process_handle_get(&a, 5), 1, RUM_SEEK_SET, &position));
    assert(process_handle_read(process_handle_get(&a, 3), bytes, 1) == 1 && bytes[0] == 'b');
    fs_test_backend_short = 2; fs_test_backend_partial_error = FS_IO_ERROR;
    assert(process_handle_read(process_handle_get(&a, 3), bytes, 4) == 2);
    fs_test_backend_short = 0; fs_test_backend_partial_error = FS_OK;
    assert(process_handle_read(process_handle_get(&a, 5), NULL, 0) == 0);
    struct rum_handle_mapping pending = {5, 10, {0}};
    assert(!process_handles_prepare_spawn(&child, &a, &pending, 1, RUM_SPAWN_NO_DEFAULT_STREAMS));
    assert(process_handle_read(process_handle_get(&child, 10), bytes, 1) == -RUM_EIO);
    assert(!process_handles_destroy(&child));
    assert(process_handle_read(process_handle_get(&a, 3), bytes, 1) == 1 && bytes[0] == 'e');
    assert(!process_handle_close(&a, 3));
    assert(fs_remove(NULL, "/DATA.BIN") == FS_BUSY && fs_replace(NULL, "/DATA.BIN", "x", 1) == FS_BUSY);
    assert(process_handle_duplicate(&a, 5, 1, 0) == 1); /* Replace a standard stream. */
    assert(process_handle_get(&a, 1) == process_handle_get(&a, 5));
    assert(process_handle_duplicate(&a, 4, 2, 0) == -RUM_EBADF); /* Read-only output. */
    assert(process_handle_duplicate(&a, 5, 5, RUM_DUP_CLOSPAWN) == 5);
    struct rum_handle_mapping map[] = {{5, 0, {0}}, {1, 1, {0}}, {4, 9, {0}}};
    assert(process_handles_prepare_spawn(&child, &a, map, 1, 0) == -RUM_EACCES);
    assert(process_handle_duplicate(&a, 5, 5, 0) == 5);
    assert(!process_handles_prepare_spawn(&child, &a, map, 3, 0));
    assert(process_handles_count(&child) == 4 && process_handle_get(&child, 9) == process_handle_get(&a, 4));
    assert(!process_handle_close(&a, 5) && !process_handle_close(&a, 1));
    assert(process_handle_read(process_handle_get(&child, 0), bytes, 1) == 1 && bytes[0] == 'f');
    assert(!process_handles_destroy(&a) && fs_remove(NULL, "/DATA.BIN") == FS_BUSY);
    assert(!process_handles_destroy(&child) && fs_stats().references == 0);
    assert(fs_replace(NULL, "/DATA.BIN", "abcdef", 6) == FS_OK);
    empty();
}
static void validation(void)
{
    struct process_handles a = {0}, child = {0};
    assert(!process_handles_initialize(&a));
    assert(process_handle_open(&a, NULL, "/DOCS", FS_READ, RUM_OPEN_DIRECTORY) == 3);
    struct rum_handle_mapping map[] = {{1, 1, {0}}, {2, 1, {0}}};
    unsigned calls = fs_test_backend_calls();
    struct process_handle_statistics before = process_handles_stats();
    assert(process_handles_prepare_spawn(&child, &a, map, 2, 0) == -RUM_EINVAL);
    map[1].target = 32;
    assert(process_handles_prepare_spawn(&child, &a, map, 2, 0) == -RUM_EINVAL);
    map[1] = (struct rum_handle_mapping){32, 2, {0}};
    assert(process_handles_prepare_spawn(&child, &a, map, 2, 0) == -RUM_EBADF);
    map[1] = (struct rum_handle_mapping){3, 2, {0}};
    assert(process_handles_prepare_spawn(&child, &a, map, 2, 0) == -RUM_EISDIR);
    map[1] = (struct rum_handle_mapping){2, 0, {0}};
    assert(process_handles_prepare_spawn(&child, &a, map, 2, 0) == -RUM_EBADF);
    map[1] = (struct rum_handle_mapping){2, 2, {1}};
    assert(process_handles_prepare_spawn(&child, &a, map, 2, 0) == -RUM_EINVAL);
    assert(process_handles_prepare_spawn(&child, &a, NULL, 33, 0) == -RUM_EINVAL);
    assert(process_handle_duplicate(&a, 1, 4, 2) == -RUM_EINVAL);
    assert(process_handle_duplicate(&a, 1, 32, 0) == -RUM_EINVAL);
    handle_test_flags = 0;
    assert(process_handle_duplicate(&a, 1, 4, 0) == -RUM_EBUSY);
    handle_test_flags = 0x200u; in_irq = true;
    assert(process_handle_duplicate(&a, 1, 4, 0) == -RUM_EBUSY);
    in_irq = false;
    assert(fs_test_backend_calls() == calls && process_handles_stats().descriptors == before.descriptors);
    assert(!process_handles_prepare_spawn(&child, &a, NULL, 0, RUM_SPAWN_NO_DEFAULT_STREAMS));
    assert(!process_handles_count(&child));
    assert(!process_handles_prepare_spawn(&child, &a, NULL, 0, 0) && process_handles_count(&child) == 3);
    assert(process_handle_get(&child, 0) != process_handle_get(&a, 0));
    assert(!process_handles_destroy(&child));
    /* Directory duplicates share iteration and rewind positions. */
    assert(process_handle_duplicate(&a, 3, RUM_DUP_ANY, 0) == 4);
    struct fs_entry entry; uint64_t position;
    assert(process_handle_readdir(process_handle_get(&a, 3), &entry) == 1);
    assert(process_handle_readdir(process_handle_get(&a, 4), &entry) == 1);
    assert(process_handle_readdir(process_handle_get(&a, 3), &entry) == 0);
    assert(!process_handle_seek(process_handle_get(&a, 4), 0, RUM_SEEK_SET, &position));
    assert(process_handle_readdir(process_handle_get(&a, 3), &entry) == 1);
    assert(!process_handles_destroy(&a)); empty();
}
static void exhaustion(void)
{
    struct process_handles tables[RUM_PROCESS_LIMIT + 1] = {0}, child = {0};
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) {
        assert(!process_handles_initialize(&tables[i]));
        unsigned end = i + 1 == RUM_PROCESS_LIMIT ? 30 : 32;
        for (unsigned h = 3; h < end; ++h)
            assert(process_handle_open(&tables[i], NULL, "/DATA.BIN", FS_READ, 0) == (rum_result_t)h);
    }
    unsigned count = process_handles_stats().descriptors;
    assert(process_handles_initialize(&tables[RUM_PROCESS_LIMIT]) == -RUM_ENOMEM);
    assert(!process_handles_count(&tables[RUM_PROCESS_LIMIT]) && process_handles_stats().descriptors == count);
    for (unsigned h = 30; h < 32; ++h)
        assert(process_handle_open(&tables[RUM_PROCESS_LIMIT - 1], NULL, "/DATA.BIN", FS_READ, 0) == (rum_result_t)h);
    assert(process_handle_duplicate(&tables[0], 3, RUM_DUP_ANY, 0) == -RUM_EMFILE);
    unsigned calls = fs_test_backend_calls();
    assert(process_handle_open(&child, NULL, "/DATA.BIN", FS_READ, 0) == -RUM_ENOMEM && fs_test_backend_calls() == calls);
    struct rum_handle_mapping map = {3, 0, {0}};
    assert(process_handles_prepare_spawn(&child, &tables[0], &map, 1, 0) == -RUM_ENOMEM);
    assert(!process_handles_prepare_spawn(&child, &tables[0], &map, 1, RUM_SPAWN_NO_DEFAULT_STREAMS));
    assert(process_handles_stats().aliases == 1 && !process_handles_destroy(&child));
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) assert(!process_handles_destroy(&tables[i]));
    empty();
}
int main(void)
{
    test_page_budget(-1);
    assert(heap_initialize() && test_files_initialize() && test_files_mount());
    assert(test_file_put("DATA.BIN", "abcdef", 6));
    aliases(); validation(); exhaustion();
    assert(!fs_stats().references && !fs_test_backend_pins() && fs_unmount_disk() == FS_OK);
    puts("PASS: shared offsets/errors, descriptor mapping, authority, pinning, limits and rollback");
    return 0;
}
