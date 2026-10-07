#include <rum/fs.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/ramfs.h>
#include "fs-backend.h"
#include "fs-checks.h"

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void path_error(const char *path, enum fs_error expected, const char *name)
{
    unsigned calls = fs_test_backend_calls();
    struct fs_information info;
    fs_reference ref = 0;
    fs_check(fs_stat_path(NULL, path, &info) == expected && fs_open(NULL, path, FS_READ, &ref) == expected &&
             !ref && fs_remove(NULL, path) == expected && fs_replace(NULL, path, "x", 1) == expected &&
             fs_mkdir(NULL, path) == expected && calls == fs_test_backend_calls(), name);
}
static fs_reference refs[FS_REFERENCE_LIMIT];
static void *guards[HEAP_LIMIT / RUM_PAGE_SIZE];

static void ram_writes(void)
{
    static const unsigned char binary[] = {0, 'r', 0xff, 'u', 'm', '\n'};
    fs_reference ref, second;
    struct fs_information info;
    unsigned char bytes[sizeof binary];
    unsigned char *payload = kmalloc(RAMFS_FILE_LIMIT);
    fs_check(payload != NULL, "allocate RAM file limit payload");
    memset(payload, 0xa5, RAMFS_FILE_LIMIT);
    fs_check(fs_replace(NULL, "/rw-test", binary, sizeof binary) == FS_OK &&
             fs_open(NULL, "rw-test", FS_READ | FS_WRITE, &ref) == FS_OK &&
             fs_open(NULL, "rw-test", FS_READ, &second) == FS_OK, "open shared RAM read write object");
    unsigned count = 0;
    while (count < HEAP_LIMIT / RUM_PAGE_SIZE && (guards[count] = kmalloc(RUM_PAGE_SIZE))) ++count;
    fs_check(count < HEAP_LIMIT / RUM_PAGE_SIZE, "reach heap exhaustion");
    struct heap_statistics heap = heap_stats();
    fs_check(fs_write(ref, 0, payload, RAMFS_FILE_LIMIT).error == FS_NO_MEMORY &&
             fs_stat(second, &info) == FS_OK && info.size == sizeof binary &&
             fs_read(second, 0, bytes, sizeof bytes).transferred == sizeof binary &&
             !memcmp(bytes, binary, sizeof bytes) && heap_stats().used_bytes == heap.used_bytes &&
             heap_stats().allocations == heap.allocations, "failed RAM growth preserves bytes and heap owners");
    while (count) fs_check(kfree(guards[--count]), "release OOM guards");
    const unsigned char *borrowed; size_t size;
    fs_check(ramfs_read("rw-test", &borrowed, &size) &&
             fs_write(ref, 2, borrowed, 4).transferred == 4 &&
             fs_read(second, 0, bytes, sizeof bytes).transferred == sizeof bytes &&
             !memcmp(bytes, (const unsigned char[]){0, 'r', 0, 'r', 0xff, 'u'}, sizeof bytes),
             "overlapping borrowed RAM write uses stable identity");
    fs_check(fs_write(ref, sizeof binary + 1, "x", 1).error == FS_RANGE &&
             fs_read(second, UINT64_MAX, bytes, 1).error == FS_RANGE &&
             fs_read(second, sizeof binary, bytes, 1).transferred == 0, "RAM holes overflow and EOF");
    fs_check(fs_write(ref, 0, payload, RAMFS_FILE_LIMIT).transferred == RAMFS_FILE_LIMIT &&
             fs_stat(second, &info) == FS_OK && info.size == RAMFS_FILE_LIMIT &&
             fs_write(ref, RAMFS_FILE_LIMIT, "x", 1).error == FS_RANGE &&
             fs_read(second, RAMFS_FILE_LIMIT - 1, bytes, 1).transferred == 1 && bytes[0] == 0xa5,
             "common RAM writes retain 64 KiB boundary and exact bytes");
    fs_check(fs_close(ref) == FS_OK && fs_close(second) == FS_OK && fs_remove(NULL, "/rw-test") == FS_OK &&
             kfree(payload), "RAM write owners released");
}

void fs_checks(void)
{
    struct fs_statistics baseline = fs_stats();
    struct fs_context cwd = {0}, clone = {0};
    struct fs_information info, other;
    fs_reference ram, disk, copy, directory;
    static const unsigned char binary[] = {0, 'r', 0xff, 'u', 'm', '\n'};
    unsigned char buffer[16];
    fs_check(fs_context_initialize(&cwd) == FS_OK && equal(cwd.path, "/"), "root working directory");
    fs_check(fs_open(NULL, "/disk", FS_READ, &disk) == FS_UNAVAILABLE && !disk, "missing disk is unavailable");
    fs_test_backend_initialize();
    fs_check(ramfs_put("disk", binary, sizeof binary), "legacy disk filename allowed before mount");
    fs_check(fs_mount_disk(fs_test_backend()) == FS_EXISTS &&
             fs_open(NULL, "/disk", FS_READ, &ram) == FS_OK, "mount cannot hide RAM file");
    fs_check(fs_read(ram, 0, buffer, sizeof buffer).transferred == sizeof binary &&
             !memcmp(buffer, binary, sizeof binary) && fs_close(ram) == FS_OK && ramfs_remove("disk"),
             "colliding RAM file keeps bytes");
    fs_check(fs_mount_disk(fs_test_backend()) == FS_OK && fs_mount_disk(fs_test_backend()) == FS_BUSY &&
             !ramfs_put("disk", NULL, 0), "mount reserves RAM namespace");
    fs_check(fs_stat_path(NULL, "/disk/docs/./nest/../readme.txt", &info) == FS_OK &&
             fs_stat_path(NULL, "//disk//DOCS//README.TXT", &other) == FS_OK &&
             info.identity.object == other.identity.object && info.identity.mount == FS_MOUNT_DISK,
             "equivalent paths and FAT case folding");
    fs_check(fs_stat_path(NULL, "/../..//.", &info) == FS_OK && info.identity.mount == FS_MOUNT_RAM &&
             fs_open(NULL, "/disk/ROOT.TXT", 0, &disk) == FS_INVALID &&
             fs_open(NULL, "/disk", FS_WRITE, &disk) == FS_IS_DIRECTORY &&
             fs_replace(NULL, "/disk/abcdefgh.xyz", binary, sizeof binary) == FS_OK &&
             fs_remove(NULL, "/disk/ABCDEFGH.XYZ") == FS_OK, "root traversal, modes and maximum 8.3 name");
    fs_check(fs_stat_path(NULL, "/disk/MISSING/../ROOT.TXT", &info) == FS_NOT_FOUND &&
             fs_stat_path(NULL, "/disk/DOCS/README.TXT/..", &info) == FS_NOT_DIRECTORY &&
             fs_stat_path(NULL, "/disk/ROOT.TXT/.", &info) == FS_NOT_DIRECTORY &&
             fs_stat_path(NULL, "/disk/ROOT.TXT/", &info) == FS_NOT_DIRECTORY,
             "semantic traversal keeps missing and file components");
    fs_check(fs_context_chdir(&cwd, "/disk//docs/./nest/..") == FS_OK && equal(cwd.path, "/disk/DOCS") &&
             fs_context_clone(&cwd, &clone) == FS_OK && fs_context_chdir(&clone, "../EMPTY") == FS_OK &&
             equal(clone.path, "/disk/EMPTY") && equal(cwd.path, "/disk/DOCS"), "independent cloned working directory");
    fs_reference before = cwd.directory;
    unsigned validation_calls = fs_test_backend_calls();
    fs_check(fs_context_chdir(&cwd, "../..") == FS_MOUNT_ESCAPE &&
             validation_calls == fs_test_backend_calls() && cwd.directory == before,
             "relative mount escape rejected before backend");
    fs_check(fs_context_chdir(&cwd, "README.TXT") == FS_NOT_DIRECTORY &&
             fs_context_chdir(&cwd, "MISSING") == FS_NOT_FOUND && cwd.directory == before,
             "failed chdir keeps owner unchanged");
    fs_check(fs_open(&cwd, "readme.txt", FS_READ | FS_WRITE, &disk) == FS_OK &&
             fs_read(disk, 0, buffer, sizeof buffer).transferred == sizeof binary &&
             !memcmp(buffer, binary, sizeof binary), "relative disk read preserves exact binary bytes");
    unsigned calls = fs_test_backend_calls();
    fs_check(fs_read(disk, UINT64_MAX, buffer, 2).error == FS_RANGE &&
             fs_write(disk, UINT64_MAX - 1, buffer, 3).error == FS_RANGE &&
             fs_read(disk, 0, NULL, 1).error == FS_INVALID &&
             fs_read(disk, UINT64_MAX, NULL, 0).error == FS_OK && calls == fs_test_backend_calls(),
             "invalid and zero byte I/O never reaches backend");
    path_error("/disk/../notes", FS_MOUNT_ESCAPE, "reject mounted root escape before backend");
    path_error("/disk/DOCS/../../ROOT.TXT", FS_MOUNT_ESCAPE, "reject nested mount escape before backend");
    path_error("/disk/TOOLONGXX.TXT", FS_NAME_TOO_LONG, "reject long FAT base before backend");
    path_error("/disk/A.TOOL", FS_NAME_TOO_LONG, "reject long FAT extension before backend");
    path_error("/disk/.BAD", FS_INVALID, "reject unsupported FAT dot name");
    path_error("/disk/A..B", FS_INVALID, "reject multiple FAT dots");
    path_error("/disk/bad name", FS_INVALID, "reject whitespace");
    path_error("/disk/bad\\name", FS_INVALID, "reject backslash");
    path_error("/disk/bad\xff", FS_INVALID, "reject non ASCII");
    path_error("", FS_INVALID, "reject empty paths");
    char path[FS_PATH_CAPACITY + 1];
    memset(path, '/', FS_PATH_CAPACITY); path[FS_PATH_CAPACITY] = 0;
    path_error(path, FS_PATH_TOO_LONG, "reject overlong raw path");
    path[FS_PATH_CAPACITY - 1] = 0;
    fs_check(fs_stat_path(NULL, path, &info) == FS_OK && info.kind == FS_DIRECTORY,
             "maximum raw path length with repeated separators");
    char name[FS_NAME_CAPACITY + 1]; memset(name, 'n', FS_NAME_CAPACITY); name[FS_NAME_CAPACITY] = 0;
    path_error(name, FS_NAME_TOO_LONG, "reject overlong RAM component");
    name[FS_NAME_CAPACITY - 1] = 0;
    fs_check(fs_replace(NULL, name, binary, sizeof binary) == FS_OK && fs_open(NULL, name, FS_READ, &ram) == FS_OK &&
             fs_read(ram, 0, buffer, sizeof buffer).transferred == sizeof binary && !memcmp(buffer, binary, sizeof binary),
             "maximum RAM filename keeps bytes and rules");
    fs_check(fs_remove(NULL, name) == FS_BUSY && fs_replace(NULL, name, NULL, 0) == FS_BUSY &&
             !ramfs_remove(name) && !ramfs_put(name, NULL, 0), "legacy and common mutations honor RAM retention");
    fs_check(fs_write(ram, 0, binary, 1).error == FS_ACCESS && fs_close(ram) == FS_OK &&
             fs_remove(NULL, name) == FS_OK, "read access and RAM removal after close");
    fs_check(fs_duplicate(disk, &copy) == FS_OK && fs_remove(&cwd, "ALIAS.TXT") == FS_BUSY &&
             fs_replace(&cwd, "ALIAS.TXT", NULL, 0) == FS_BUSY && fs_unmount_disk() == FS_BUSY,
             "alias identities and duplicated handles pin objects");
    fs_check(fs_close(disk) == FS_OK && fs_close(disk) == FS_INVALID &&
             fs_read(disk, 0, buffer, 1).error == FS_INVALID && fs_remove(&cwd, "README.TXT") == FS_BUSY,
             "stale references and remaining duplicate");
    fs_test_backend_reenter = true;
    fs_check(fs_read(copy, 0, buffer, 1).error == FS_OK && fs_test_backend_reentry_result == FS_BUSY,
             "reentrant request fails without spinning");
    fs_test_backend_reenter = false;
    fs_test_backend_failure = FS_TIMEOUT;
    struct fs_io_result result = fs_read(copy, 0, buffer, 1);
    fs_check(result.error == FS_TIMEOUT && result.device_status == 0x51 && result.device_error == 4 &&
             fs_flush(copy) == FS_TIMEOUT, "backend diagnostics and flush errors survive translation");
    fs_test_backend_failure = FS_OK;
    fs_check(fs_write(copy, sizeof binary, "!", 1).transferred == 1 && fs_stat(copy, &info) == FS_OK &&
             info.size == sizeof binary + 1 && fs_close(copy) == FS_OK, "write and stat share object identity");
    fs_check(fs_open(NULL, "/disk", FS_READ, &directory) == FS_OK &&
             fs_read(directory, 0, buffer, 1).error == FS_IS_DIRECTORY, "directory read rejects byte IO");
    uint64_t cursor = 0; struct fs_entry entry;
    fs_check(fs_readdir(directory, &cursor, &entry) == FS_OK && equal(entry.name, "DOCS"), "disk directory iteration");
    fs_test_backend_bad_cursor = true; uint64_t saved = cursor;
    fs_check(fs_readdir(directory, &cursor, &entry) == FS_IO_ERROR && cursor == saved, "reject non advancing backend cursor");
    fs_test_backend_bad_cursor = false;
    cursor = UINT64_MAX;
    fs_check(fs_readdir(directory, &cursor, &entry) == FS_END && cursor == UINT64_MAX && fs_close(directory) == FS_OK,
             "directory cursor arithmetic bound");
    fs_check(fs_open(NULL, "/", FS_READ, &directory) == FS_OK, "open RAM root"); cursor = 0;
    fs_check(fs_readdir(directory, &cursor, &entry) == FS_OK && equal(entry.name, "disk") &&
             entry.kind == FS_DIRECTORY && fs_close(directory) == FS_OK, "root contains mount entry");
    fs_check(fs_replace(NULL, "/disk/EMPTY/NEW.TXT", binary, sizeof binary) == FS_OK &&
             fs_remove(NULL, "/disk/EMPTY") == FS_BUSY, "working directory cannot be removed");
    fs_check(fs_context_destroy(&clone) == FS_OK && fs_remove(NULL, "/disk/EMPTY") == FS_NOT_EMPTY &&
             fs_remove(NULL, "/disk/EMPTY/NEW.TXT") == FS_OK && fs_remove(NULL, "/disk/EMPTY") == FS_OK,
             "directory nonempty then removal");
    fs_check(fs_mkdir(NULL, "/disk/NEW/") == FS_OK && fs_mkdir(NULL, "/disk/NEW") == FS_EXISTS &&
             fs_replace(NULL, "/disk/NEW", NULL, 0) == FS_IS_DIRECTORY && fs_remove(NULL, "/disk/NEW") == FS_OK &&
             fs_mkdir(NULL, "/ramdir") == FS_UNSUPPORTED && fs_remove(NULL, "/disk") == FS_BUSY,
             "directory creation, backend support and mount protection");
    /* Depth includes the final file name, but excludes /disk itself. */
    memcpy(path, "/disk", 6); unsigned length = 5;
    for (unsigned i = 0; i < FS_PATH_DEPTH; ++i) {
        memcpy(path + length, "/D", 3); length += 2;
        fs_check(fs_mkdir(NULL, path) == FS_OK, "create maximum depth directories");
    }
    fs_check(fs_stat_path(NULL, path, &info) == FS_OK, "maximum depth resolves");
    memcpy(path + length, "/D", 3);
    path_error(path, FS_TOO_DEEP, "reject depth overflow before backend");
    path[length] = 0;
    for (unsigned i = 0; i < FS_PATH_DEPTH; ++i) {
        fs_check(fs_remove(NULL, path) == FS_OK, "remove depth fixtures"); length -= 2; path[length] = 0;
    }
    unsigned pins = fs_test_backend_pins();
    fs_test_backend_failure = FS_NO_MEMORY;
    fs_check(fs_open(NULL, "/disk/ROOT.TXT", FS_READ, &disk) == FS_NO_MEMORY && !disk &&
             pins == fs_test_backend_pins(), "retain failure has no published reference");
    fs_test_backend_failure = FS_OK;
    unsigned references = fs_stats().references, count = 0;
    while (fs_duplicate(cwd.directory, &refs[count]) == FS_OK) { ++count; fs_check(count < FS_REFERENCE_LIMIT, "reference bound"); }
    fs_check(count + references == FS_REFERENCE_LIMIT && fs_context_chdir(&cwd, "/") == FS_NO_SPACE &&
             cwd.directory == before, "reference exhaustion and chdir rollback");
    while (count) fs_check(fs_close(refs[--count]) == FS_OK, "release exhausted references");
    fs_check(fs_context_destroy(&cwd) == FS_OK && fs_unmount_disk() == FS_OK && !fs_test_backend_pins(),
             "destroy cwd releases all disk owners");
    /* Exhaust distinct objects separately from reference slots. */
    fs_check(fs_mount_disk(fs_test_backend()) == FS_OK, "remount writable test tree");
    unsigned objects = fs_stats().objects;
    for (unsigned i = 0; i < FS_OBJECT_LIMIT - objects; ++i) {
        char filename[] = "/disk/F000.TXT";
        filename[7] = (char)('0' + i / 100); filename[8] = (char)('0' + i / 10 % 10); filename[9] = (char)('0' + i % 10);
        fs_check(fs_replace(NULL, filename, NULL, 0) == FS_OK && fs_open(NULL, filename, FS_READ, &refs[i]) == FS_OK,
                 "fill object table");
    }
    fs_check(fs_open(NULL, "/disk/ROOT.TXT", FS_READ, &disk) == FS_NO_SPACE && !disk,
             "object table limit rejects without retaining");
    for (unsigned i = 0; i < FS_OBJECT_LIMIT - objects; ++i) fs_check(fs_close(refs[i]) == FS_OK, "release object table");
    fs_check(fs_unmount_disk() == FS_OK, "unmount after table cleanup");
    struct fs_backend readonly = *fs_test_backend(); readonly.read_only = true;
    fs_check(fs_mount_disk(&readonly) == FS_OK && fs_open(NULL, "/disk/ROOT.TXT", FS_WRITE, &disk) == FS_READ_ONLY &&
             fs_replace(NULL, "/disk/NEW.TXT", binary, sizeof binary) == FS_READ_ONLY &&
             fs_remove(NULL, "/disk/ROOT.TXT") == FS_READ_ONLY && fs_mkdir(NULL, "/disk/NEW") == FS_READ_ONLY,
             "read only mount refuses every mutation");
    fs_check(fs_open(NULL, "/disk/ROOT.TXT", FS_READ, &disk) == FS_OK && fs_stat(disk, &info) == FS_OK &&
             fs_close(disk) == FS_OK && fs_unmount_disk() == FS_OK && fs_mount_disk(fs_test_backend()) == FS_OK &&
             fs_stat_path(NULL, "/disk/ROOT.TXT", &other) == FS_OK &&
             info.identity.generation != other.identity.generation && info.identity.object == other.identity.object,
             "mount generation separates reused backend identities");
    fs_check(fs_unmount_disk() == FS_OK && fs_stats().references == baseline.references &&
             fs_stats().objects == baseline.objects && fs_stats().mounts == baseline.mounts && !fs_test_backend_pins(),
             "filesystem ownership returns to baseline");
    ram_writes();
    fs_check(fs_stats().references == baseline.references && fs_stats().objects == baseline.objects,
             "RAM write test restores reference baseline");
    struct fs_backend system = *fs_test_backend(); system.naming = FS_NAMES_RAM;
    fs_check(fs_mount_system(&system) == FS_INVALID, "system mount requires a read-only backend");
    system.read_only = true;
    fs_check(ramfs_put("rum", binary, sizeof binary) && fs_mount_system(&system) == FS_EXISTS &&
             ramfs_remove("rum"), "system mount cannot hide a legacy RAM file");
    fs_check(fs_mount_system(&system) == FS_OK && fs_mount_system(&system) == FS_BUSY &&
             !ramfs_put("rum", "x", 1), "permanent system mount reserves its root name");
    fs_check(fs_open(NULL, "/rum/ROOT.TXT", FS_READ, &ram) == FS_OK &&
             fs_read(ram, 0, buffer, sizeof buffer).transferred == 4 && !memcmp(buffer, "root", 4) &&
             fs_close(ram) == FS_OK, "read through the system backend");
    fs_check(fs_open(NULL, "/rum/ROOT.TXT", FS_WRITE, &ram) == FS_READ_ONLY &&
             fs_replace(NULL, "/rum/ROOT.TXT", "x", 1) == FS_READ_ONLY &&
             fs_remove(NULL, "/rum/ROOT.TXT") == FS_READ_ONLY && fs_mkdir(NULL, "/rum/NEW") == FS_READ_ONLY &&
             fs_remove(NULL, "/rum") == FS_BUSY && fs_replace(NULL, "/rum", "x", 1) == FS_IS_DIRECTORY,
             "system files and mounted root reject mutation");
    path_error("/rum/../disk/ROOT.TXT", FS_MOUNT_ESCAPE, "system traversal rejected before backend lookup");
    fs_check(fs_context_initialize(&cwd) == FS_OK && fs_context_chdir(&cwd, "//rum/./DOCS") == FS_OK &&
             equal(cwd.path, "/rum/DOCS") && fs_context_chdir(&cwd, "..") == FS_OK &&
             equal(cwd.path, "/rum") && fs_context_chdir(&cwd, "..") == FS_MOUNT_ESCAPE &&
             fs_context_chdir(&cwd, "/") == FS_OK && fs_context_destroy(&cwd) == FS_OK,
             "system cwd normalizes and cannot leave the mount through dot-dot");
    fs_check(fs_open(NULL, "/", FS_READ, &directory) == FS_OK, "open root with virtual system directory");
    cursor = 0; unsigned system_entries = 0;
    while (fs_readdir(directory, &cursor, &entry) == FS_OK)
        if (equal(entry.name, "rum")) { ++system_entries; fs_check(entry.kind == FS_DIRECTORY, "system directory kind"); }
    fs_check(system_entries == 1 && fs_close(directory) == FS_OK &&
             fs_stats().references == baseline.references && fs_stats().objects == baseline.objects &&
             fs_stats().mounts == baseline.mounts + 1 && !fs_test_backend_pins(), "system mount reference cleanup");
}
