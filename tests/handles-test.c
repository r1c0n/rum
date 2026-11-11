#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/pipe.h>
#include "fs-backend.h"
#include "page-backend.h"
#include "test-files.h"
#include "pipe-task-backend.h"

uint32_t handle_test_flags = 0x200u;
static bool in_irq;
bool irq_in_handler(void) { return in_irq; }
static void empty(void)
{
    struct process_handle_statistics s = process_handles_stats();
    assert(!s.descriptors && !s.descriptions && !s.aliases);
    assert(!object_stats().total.objects && !object_stats().total.references[OBJECT_TABLE] &&
           !object_stats().total.references[OBJECT_OPERATION]);
}
static struct process_handles *pending_table;
static void close_pending(void)
{
    assert(pipe_stats().operations == 1);
    assert(!process_handle_close(pending_table, 3) && !process_handle_close(pending_table, 4));
    assert(process_handles_stats().descriptors == 3 && process_handles_stats().descriptions == 4 &&
           !process_handles_stats().aliases && object_stats().total.references[OBJECT_OPERATION] == 1);
}
static void retained_operations(void)
{
    struct process_handles table = {0};
    assert(!process_handles_initialize(&table));
    assert(process_handle_open(&table, NULL, "/DATA.BIN", FS_READ, 0) == 3);
    struct process_handle *operation;
    assert(!process_handle_acquire(&table, 3, &operation));
    assert(!process_handle_close(&table, 3) && fs_remove(NULL, "/DATA.BIN") == FS_BUSY &&
           fs_replace(NULL, "/DATA.BIN", "x", 1) == FS_BUSY);
    char bytes[8]; assert(process_handle_read(operation, bytes, 2) == 2 && bytes[0] == 'a');
    assert(!object_release(&operation->object, OBJECT_OPERATION));
    for (unsigned write = 0; write < 2; ++write) {
        rum_handle_t reader, writer;
        assert(!process_handles_pipe(&table, &reader, &writer) && reader == 3 && writer == 4);
        if (write) {
            char data[RUM_PIPE_ATOMIC_WRITE] = {0};
            for (unsigned i = 0; i < RUM_PIPE_CAPACITY / sizeof data; ++i)
                assert(process_handle_write(process_handle_get(&table, writer), data, sizeof data) == sizeof data);
        }
        assert(!process_handle_acquire(&table, write ? writer : reader, &operation));
        pending_table = &table; pipe_test_before_wait = close_pending;
        rum_result_t result = write ? process_handle_write(operation, bytes, 1) : process_handle_read(operation, bytes, 1);
        assert(result == (write ? -RUM_EPIPE : 0) && !pipe_test_before_wait);
        assert(!object_release(&operation->object, OBJECT_OPERATION) && !pipe_stats().pipes);
    }
    assert(!process_handles_destroy(&table)); empty();
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
static void typed_aliases(void)
{
    struct process_handles table = {0}; struct object_handles typed = {0}, child = {0};
    rum_object_handle_t token = 19;
    assert(!process_handles_initialize(&table));
    assert(process_handle_open(&table, NULL, "/DATA.BIN", FS_READ | FS_WRITE, 0) == 3);
    unsigned refs = fs_stats().references, pins = fs_test_backend_pins();
    assert(process_handle_export(&table, 3, &typed, RUM_OBJECT_DIRECTORY, RUM_RIGHT_QUERY, 0, &token) == -RUM_EINVAL && token == 19);
    assert(!process_handle_export(&table, 3, &typed, RUM_OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT, &token));
    assert(fs_stats().references == refs && fs_test_backend_pins() == pins && process_handles_stats().descriptors == 4);
    assert(!object_handles_inherit(&child, &typed, RUM_RIGHT_READ));
    struct kernel_object *object;
    assert(object_handle_acquire(&child, token, OBJECT_FILE, RUM_RIGHT_WRITE, &object) == -RUM_EACCES);
    assert(!object_handle_acquire(&child, token, OBJECT_FILE, RUM_RIGHT_READ, &object));
    struct process_handle *handle = process_handle_from_object(object);
    char byte; uint64_t position;
    assert(handle == process_handle_get(&table, 3));
    assert(!process_handle_seek(handle, 0, RUM_SEEK_SET, &position));
    assert(process_handle_read(handle, &byte, 1) == 1 && byte == 'a');
    assert(process_handle_read(process_handle_get(&table, 3), &byte, 1) == 1 && byte == 'b');
    fs_test_backend_short = 1; fs_test_backend_partial_error = FS_IO_ERROR;
    assert(process_handle_read(handle, &byte, 2) == 1);
    fs_test_backend_short = 0; fs_test_backend_partial_error = FS_OK;
    assert(process_handle_read(process_handle_get(&table, 3), &byte, 1) == -RUM_EIO);
    assert(!process_handle_close(&table, 3) && !object_handles_destroy(&typed) && !object_handles_destroy(&child));
    assert(fs_remove(NULL, "/DATA.BIN") == FS_BUSY && fs_replace(NULL, "/DATA.BIN", "x", 1) == FS_BUSY);
    assert(!object_release(object, OBJECT_OPERATION) && fs_stats().references == refs - 1);
    assert(process_handle_open(&table, NULL, "/DATA.BIN", FS_READ, 0) == 3);
    assert(process_handle_export(&table, 3, &typed, 0, RUM_RIGHT_WRITE, 0, &token) == -RUM_EACCES);
    assert(process_handle_duplicate(&table, 3, 3, RUM_DUP_CLOSPAWN) == 3);
    assert(process_handle_export(&table, 3, &typed, 0, RUM_RIGHT_READ, RUM_OBJECT_INHERIT, &token) == -RUM_EACCES);
    assert(!process_handle_export(&table, 3, &typed, 0, RUM_RIGHT_READ, 0, &token));
    assert(!object_handles_destroy(&typed) && !process_handles_destroy(&table)); empty();
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
    for (unsigned h = 30; h < 32; ++h) {
        assert(process_handle_open(&tables[RUM_PROCESS_LIMIT - 1], NULL, "/DATA.BIN", FS_READ, 0) == (rum_result_t)h);
        if (h == 30) {
            rum_handle_t read = 123, write = 456;
            unsigned descriptions = process_handles_stats().descriptions;
            assert(process_handles_pipe(&child, &read, &write) == -RUM_ENOMEM);
            assert(read == 123 && write == 456 && !process_handles_count(&child) &&
                   process_handles_stats().descriptions == descriptions && !pipe_stats().pipes);
        }
    }
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
static void pipes_test(void)
{
    struct process_handles table = {0}, child = {0};
    rum_handle_t read, write;
    unsigned calls = fs_test_backend_calls();
    assert(!process_handles_initialize(&table) && !process_handles_pipe(&table, &read, &write));
    assert(read == 3 && write == 4 && pipe_stats().pipes == 1);
    assert(process_handle_duplicate(&table, read, RUM_DUP_ANY, 0) == 5 &&
           process_handle_duplicate(&table, write, RUM_DUP_ANY, 0) == 6);
    struct rum_handle_mapping maps[] = {{5, 0, {0}}, {6, 1, {0}}};
    assert(!process_handles_prepare_spawn(&child, &table, maps, 2, RUM_SPAWN_NO_DEFAULT_STREAMS));
    assert(!process_handle_close(&table, read) && !process_handle_close(&table, write));
    uint64_t position; struct fs_entry entry;
    assert(process_handle_seek(process_handle_get(&child, 0), 0, 0, &position) == -RUM_ESPIPE &&
           process_handle_seek(process_handle_get(&child, 1), 0, 0, &position) == -RUM_ESPIPE &&
           process_handle_readdir(process_handle_get(&child, 0), &entry) == -RUM_ENOTDIR &&
           process_handle_readdir(process_handle_get(&child, 1), &entry) == -RUM_ENOTDIR &&
           process_handle_flush(process_handle_get(&child, 1)) == -RUM_EINVAL);
    char bytes[4];
    assert(process_handle_read(process_handle_get(&child, 1), bytes, 0) == -RUM_EBADF);
    assert(process_handle_write(process_handle_get(&table, 6), "a\0b", 3) == 3 &&
           process_handle_read(process_handle_get(&child, 0), bytes, 4) == 3 && bytes[0] == 'a' && !bytes[1] && bytes[2] == 'b');
    assert(!process_handle_close(&table, 5) && process_handle_write(process_handle_get(&child, 1), "x", 1) == 1);
    assert(process_handle_read(process_handle_get(&child, 0), bytes, 1) == 1 && bytes[0] == 'x');
    assert(!process_handle_close(&child, 0) && process_handle_write(process_handle_get(&table, 6), "x", 1) == -RUM_EPIPE);
    assert(process_handle_write(process_handle_get(&table, 6), NULL, 0) == 0);
    assert(!process_handles_destroy(&table) && pipe_stats().pipes == 1 && !process_handles_destroy(&child));
    empty(); assert(!pipe_stats().pipes);
    assert(!process_handles_initialize(&table));
    for (unsigned i = 0; i < 14; ++i) assert(!process_handles_pipe(&table, &read, &write));
    read = 123; write = 456;
    unsigned descriptors = process_handles_stats().descriptors;
    assert(process_handles_pipe(&table, &read, &write) == -RUM_EMFILE && read == 123 && write == 456 &&
           process_handles_stats().descriptors == descriptors && pipe_stats().pipes == 14);
    assert(!process_handles_destroy(&table));
    struct process_handles all[RUM_KERNEL_PIPE_LIMIT] = {0};
    for (unsigned i = 0; i < RUM_KERNEL_PIPE_LIMIT; ++i) assert(!process_handles_pipe(&all[i], &read, &write));
    descriptors = process_handles_stats().descriptors;
    assert(process_handles_pipe(&table, &read, &write) == -RUM_ENOMEM &&
           !process_handles_count(&table) && process_handles_stats().descriptors == descriptors);
    for (unsigned i = 0; i < RUM_KERNEL_PIPE_LIMIT; ++i) assert(!process_handles_destroy(&all[i]));
    assert(fs_test_backend_calls() == calls && !pipe_stats().pipes); empty();
}
static void open_modes(void)
{
    struct process_handles t = {0};
    assert(!process_handles_initialize(&t));
    unsigned calls = fs_test_backend_calls();
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_READ, RUM_OPEN_CREATE) == -RUM_EINVAL);
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_WRITE, 16) == -RUM_EINVAL);
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_READ, RUM_OPEN_DIRECTORY | RUM_OPEN_APPEND) == -RUM_EINVAL);
    assert(fs_test_backend_calls() == calls);
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_WRITE, RUM_OPEN_TRUNCATE) == -RUM_ENOENT);
    assert(process_handle_open(&t, NULL, "/MISSING/FILE.TXT", FS_WRITE, RUM_OPEN_CREATE) == -RUM_ENOENT);
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_READ | FS_WRITE, RUM_OPEN_CREATE | RUM_OPEN_TRUNCATE) == 3);
    struct process_handle *a = process_handle_get(&t, 3);
    assert(process_handle_write(a, "old", 3) == 3);
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_WRITE, RUM_OPEN_TRUNCATE) == -RUM_EBUSY);
    assert(!process_handle_close(&t, 3));
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_READ | FS_WRITE, RUM_OPEN_TRUNCATE) == 3);
    a = process_handle_get(&t, 3);
    char bytes[16]; assert(process_handle_read(a, bytes, sizeof bytes) == 0);
    assert(!process_handle_close(&t, 3));
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_READ | FS_WRITE, RUM_OPEN_APPEND) == 3);
    assert(process_handle_open(&t, NULL, "/MODES.TXT", FS_WRITE, RUM_OPEN_APPEND) == 4);
    a = process_handle_get(&t, 3);
    struct process_handle *b = process_handle_get(&t, 4);
    assert(process_handle_write(a, "ab", 2) == 2 && a->offset == 2);
    assert(process_handle_write(b, "cd", 2) == 2 && b->offset == 4);
    uint64_t position;
    assert(!process_handle_seek(a, 0, RUM_SEEK_SET, &position));
    assert(process_handle_write(a, "ef", 2) == 2 && a->offset == 6);
    assert(process_handle_duplicate(&t, 3, 5, 0) == 5 && process_handle_get(&t, 5)->append);
    fs_test_backend_short = 1; fs_test_backend_partial_error = FS_IO_ERROR;
    assert(process_handle_write(b, "gh", 2) == 1 && b->offset == 7);
    fs_test_backend_short = 0; fs_test_backend_partial_error = FS_OK;
    assert(process_handle_write(b, NULL, 0) == 0);
    assert(process_handle_write(b, "h", 1) == -RUM_EIO && b->offset == 7);
    assert(process_handle_write(b, "h", 1) == 1 && b->offset == 8);
    assert(!process_handle_seek(a, 0, RUM_SEEK_SET, &position));
    assert(process_handle_read(a, bytes, sizeof bytes) == 8 && !memcmp(bytes, "abcdefgh", 8));
    assert(fs_replace(NULL, "/MODES.TXT", "x", 1) == FS_BUSY);
    assert(!process_handles_destroy(&t) && fs_remove(NULL, "/MODES.TXT") == FS_OK);
    empty();
}
static void resource_counts(void)
{
    struct process_handles a = {0}, foreign = {0}; struct object_handles typed = {0};
    struct rum_resource_information info;
    assert(!process_handles_initialize(&a) && !process_handles_initialize(&foreign));
    assert(process_handle_open(&foreign, NULL, "/DATA.BIN", FS_READ, 0) == 3);
    assert(!process_resources_read(&a, &typed, &info));
    assert(info.version == 1 && info.size == sizeof info && info.descriptors == 3 && !info.typed_handles &&
           info.objects == 3 && info.table_references == 3 && info.types[RUM_OBJECT_CONSOLE_INPUT] == 1 &&
           info.types[RUM_OBJECT_CONSOLE_OUTPUT] == 2 && !info.types[RUM_OBJECT_FILE]);
    assert(process_handle_open(&a, NULL, "/DATA.BIN", FS_READ, 0) == 3 && process_handle_duplicate(&a, 3, 4, 0) == 4);
    rum_object_handle_t tokens[RUM_OBJECT_HANDLE_LIMIT];
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i)
        assert(!process_handle_export(&a, 3, &typed, RUM_OBJECT_FILE, RUM_RIGHT_READ, 0, &tokens[i]));
    struct object_statistics before = object_stats(); unsigned calls = fs_test_backend_calls();
    assert(!process_resources_read(&a, &typed, &info) && info.descriptors == 5 && info.typed_handles == 32 &&
           info.objects == 4 && info.table_references == 37 && info.types[RUM_OBJECT_FILE] == 1);
    struct object_statistics after = object_stats(); assert(!memcmp(&before, &after, sizeof before) && calls == fs_test_backend_calls());
    in_irq = true; assert(process_resources_read(&a, &typed, &info) == -RUM_EBUSY); in_irq = false;
    assert(process_resources_read(NULL, &typed, &info) == -RUM_EINVAL);
    assert(!object_handles_destroy(&typed) && !process_handles_destroy(&a) && !process_handles_destroy(&foreign)); empty();
}

int main(void)
{
    test_page_budget(-1);
    assert(heap_initialize() && test_files_initialize() && test_files_mount());
    assert(test_file_put("DATA.BIN", "abcdef", 6));
    retained_operations(); typed_aliases(); aliases(); validation(); exhaustion(); pipes_test(); open_modes(); resource_counts();
    assert(!fs_stats().references && !fs_test_backend_pins() && fs_unmount_disk() == FS_OK);
    puts("PASS: shared offsets/errors, descriptor mapping, authority, pinning, limits and rollback");
    return 0;
}
