#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/cpu.h>
#include <rum/abi/error.h>
#include <rum/handles.h>
#include <rum/fs_policy.h>
#include <rum/heap.h>
#include "fs-backend.h"
#include "page-backend.h"
#include "test-files.h"

uint32_t handle_test_flags = 0x200;
static bool user;
bool irq_in_handler(void) { return false; }
static bool trusted(void) { return !user; }
static bool block_allowed(void) { return trusted() || fs_operation_active(); }
static unsigned driver_calls;
static struct block_result read_disk(struct block_device *d, uint64_t lba, uint64_t count, void *p)
{
    (void)d; (void)lba; (void)p; ++driver_calls; return (struct block_result){ .completed = count };
}
static const struct block_operations disk_ops = { .read = read_disk };
static unsigned scan_mode, scan_calls;
static enum fs_error scan_lookup(void *context, uint64_t parent, const char *name, struct fs_node *node)
{
    (void)context; (void)name;
    *node = (struct fs_node){ .id = parent == 1 ? 2 : scan_mode == 1 ? parent + 1 : 3,
        .kind = parent == 1 || scan_mode == 1 ? FS_DIRECTORY : FS_FILE };
    return FS_OK;
}
static enum fs_error scan_directory(void *context, uint64_t id, uint64_t cursor, struct fs_entry *entry, uint64_t *next)
{
    (void)context; (void)id; ++scan_calls;
    if (scan_mode == 1 && cursor) return FS_END;
    *entry = (struct fs_entry){ .kind = scan_mode == 1 ? FS_DIRECTORY : FS_FILE, .name = "E" };
    *next = scan_mode == 2 ? cursor : cursor + 1; return FS_OK;
}
static void index_limits(void)
{
    const struct fs_operations operations = { .lookup = scan_lookup, .readdir = scan_directory };
    const struct fs_backend backend = { .operations = &operations, .root = 1, .naming = FS_NAMES_FAT83 };
    struct fs_system_index index;
    scan_mode = 0; scan_calls = 0;
    assert(fs_system_index_build(&index, &backend) == FS_NO_SPACE && !index.valid && scan_calls <= FS_SYSTEM_OBJECT_LIMIT);
    scan_mode = 1; scan_calls = 0;
    assert(fs_system_index_build(&index, &backend) == FS_TOO_DEEP && !index.valid && scan_calls <= FS_PATH_DEPTH * 2);
    scan_mode = 2; scan_calls = 0;
    assert(fs_system_index_build(&index, &backend) == FS_IO_ERROR && !index.valid && scan_calls == 1);
}
int main(void)
{
    index_limits();
    test_page_budget(-1);
    assert(heap_initialize() && test_files_initialize() && test_files_mount());
    assert(fs_set_authority_policy(trusted, trusted) && block_set_authority_policy(block_allowed));
    assert(fs_mkdir(NULL, "/RUM") == FS_OK && fs_mkdir(NULL, "/RUM/SUB") == FS_OK);
    assert(fs_replace(NULL, "/RUM/CORE.BIN", "trusted", 7) == FS_OK && fs_replace(NULL, "/RUM/SUB/CFG", "cfg", 3) == FS_OK);
    assert(fs_test_backend_alias("SYS", "/RUM") && fs_test_backend_alias("CORE.ALS", "/RUM/CORE.BIN"));
    struct process_handles table = {0}; struct object_handles objects = {0}, child = {0};
    assert(!process_handles_initialize(&table) && process_handle_open(&table, NULL, "/CORE.ALS", 3, 0) == 3);
    rum_object_handle_t token;
    assert(!process_handle_export(&table, 3, &objects, RUM_OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT, &token));
    struct process_handle *handle = process_handle_get(&table, 3);
    struct fs_context cwd = {0}; assert(fs_context_initialize(&cwd) == FS_OK && fs_context_chdir(&cwd, "/RUM/SUB") == FS_OK);
    unsigned writes = fs_test_backend_mutation_calls();
    struct fs_statistics baseline = fs_stats(); struct object_statistics refs = object_stats();
    user = true;
    const char *paths[] = {"/rum/core.bin", "/RUM//./CORE.BIN", "/DOCS/../RUM/SUB/../CORE.BIN", "/SYS/CORE.BIN", "/CORE.ALS"};
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        fs_reference out = 0;
        assert(fs_open_flags(NULL, paths[i], FS_WRITE, FS_OPEN_CREATE | FS_OPEN_TRUNCATE, &out) == FS_ACCESS && !out);
        assert(fs_replace(NULL, paths[i], "bad", 3) == FS_ACCESS && fs_remove(NULL, paths[i]) == FS_ACCESS);
    }
    assert(fs_replace(&cwd, "../CORE.BIN", "bad", 3) == FS_ACCESS && fs_mkdir(NULL, "/SYS/NEW") == FS_ACCESS);
    assert(fs_rename(NULL, "/RUM/CORE.BIN", "/MOVED", false) == FS_ACCESS);
    assert(fs_rename(NULL, "/CORE.ALS", "/MOVED", false) == FS_ACCESS &&
           fs_rename(NULL, "/ROOT.TXT", "/CORE.ALS", true) == FS_ACCESS);
    assert(fs_rename(NULL, "/ROOT.TXT", "/RUM/NEW", false) == FS_ACCESS && fs_remove(NULL, "/RUM") == FS_ACCESS);
    assert(fs_write(handle->reference, 0, "bad", 3).error == FS_ACCESS && fs_truncate(handle->reference, 0) == FS_ACCESS);
    handle->pending_error = -RUM_EIO;
    assert(process_handle_write(handle, "bad", 3) == -RUM_EACCES && handle->pending_error == -RUM_EIO && !handle->offset);
    handle->pending_error = 0;
    assert(process_handle_duplicate(&table, 3, 4, 0) == -RUM_EACCES);
    struct kernel_object *operation;
    assert(object_handle_acquire(&objects, token, OBJECT_FILE, RUM_RIGHT_WRITE, &operation) == -RUM_EACCES);
    struct rum_object_request duplicate = { .version = 1, .size = sizeof duplicate, .operation = RUM_OBJECT_DUPLICATE,
        .handle = token, .rights = RUM_RIGHT_WRITE };
    assert(object_handle_request(&objects, &duplicate) == -RUM_EACCES && !duplicate.result_handle);
    assert(object_handles_inherit(&child, &objects, RUM_RIGHT_MASK) == -RUM_EACCES && !object_handles_count(&child));
    struct rum_object_inheritance grant = { .handle = token, .type = RUM_OBJECT_FILE, .rights = RUM_RIGHT_READ };
    assert(!object_handles_prepare_spawn(&child, &objects, &grant, 1) && !object_handles_destroy(&child));
    fs_reference copy = 0; assert(fs_duplicate(handle->reference, &copy) == FS_ACCESS && !copy);
    uint64_t copied = 0; assert(fs_install_system_file("/ROOT.TXT", "/RUM/NEW", &copied) == FS_ACCESS && !copied);
    assert(fs_unmount_disk() == FS_ACCESS && fs_mount_disk(fs_test_backend()) == FS_ACCESS);
    struct block_device disk = { .online = true, .sector_size = 512, .sector_count = 8, .operations = &disk_ops };
    char bytes[512];
    assert(block_read(&disk, 0, 1, bytes, sizeof bytes).error == BLOCK_ACCESS && !driver_calls && !disk.busy);
    assert(block_write(&disk, 0, 1, bytes, sizeof bytes).error == BLOCK_ACCESS && block_flush(&disk).error == BLOCK_ACCESS);
    assert(fs_test_backend_mutation_calls() == writes && fs_stats().references == baseline.references);
    struct object_statistics after = object_stats(); assert(!memcmp(&refs, &after, sizeof refs));
    assert(fs_replace(NULL, "/NOTE.TXT", "ok", 2) == FS_OK && fs_mkdir(NULL, "/APPS") == FS_OK);
    assert(fs_rename(NULL, "/NOTE.TXT", "/APPS/NOTE.TXT", false) == FS_OK);
    user = false;
    assert(fs_write(handle->reference, 0, "trusted", 7).error == FS_OK);
    assert(!object_handles_destroy(&objects) && !process_handles_destroy(&table) && fs_context_destroy(&cwd) == FS_OK);
    assert(fs_install_system_file("/ROOT.TXT", "/RUM/INST.BIN", &copied) == FS_OK && copied == 4);
    assert(fs_install_system_file("/RUM/INST.BIN", "/rum/inst.bin", &copied) == FS_INVALID);
    fs_test_backend_bad_cursor = true;
    assert(fs_replace(NULL, "/TRIGGER", "x", 1) == FS_OK);
    user = true; assert(fs_replace(NULL, "/DATA.TXT", "x", 1) == FS_ACCESS);
    user = false; fs_test_backend_bad_cursor = false;
    assert(fs_replace(NULL, "/TRIGGER", "x", 1) == FS_OK);
    user = true; assert(fs_replace(NULL, "/DATA.TXT", "x", 1) == FS_OK);
    user = false;
    assert(!object_stats().total.objects && !fs_stats().references && fs_unmount_disk() == FS_OK);
    puts("PASS: system identity/path protection, retained handles, grant ceilings, raw-device denial, maintenance and fail-closed policy");
}
