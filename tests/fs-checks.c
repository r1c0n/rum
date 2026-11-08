#include <rum/fs.h>
#include <rum/memory.h>
#include "fs-backend.h"
#include "fs-checks.h"
void fs_checks(void)
{
    fs_test_backend_initialize();
    if (!fs_stats().mounts) fs_check(fs_mount_disk(fs_test_backend()) == FS_OK, "mount root");
    struct fs_statistics baseline = fs_stats();
    struct fs_context cwd = {0};
    fs_check(fs_context_initialize(&cwd) == FS_OK && fs_context_chdir(&cwd, "/docs/nest/..") == FS_OK, "relative root context");
    fs_reference file, alias;
    fs_check(fs_open(&cwd, "readme.txt", FS_READ | FS_WRITE, &file) == FS_OK &&
             fs_open(&cwd, "alias.txt", FS_READ, &alias) == FS_OK, "case-insensitive names and backend identity");
    fs_check(fs_remove(&cwd, "README.TXT") == FS_BUSY && fs_replace(&cwd, "ALIAS.TXT", "x", 1) == FS_BUSY, "open identities prevent removal and replacement");
    unsigned char data[16] = {0};
    fs_check(fs_read(file, 0, data, sizeof data).transferred == 6 && data[0] == 0 && data[2] == 255, "binary bytes");
    fs_check(fs_write(alias, 0, "x", 1).error == FS_ACCESS, "access mode");
    fs_check(fs_read(file, UINT64_MAX, data, 2).error == FS_RANGE, "offset overflow");
    fs_test_backend_short = 2; fs_test_backend_partial_error = FS_IO_ERROR;
    struct fs_io_result io = fs_read(file, 0, data, sizeof data);
    fs_check(io.transferred == 2 && io.error == FS_IO_ERROR, "partial error");
    fs_test_backend_short = 0; fs_test_backend_partial_error = FS_OK;
    fs_check(fs_close(alias) == FS_OK && fs_close(file) == FS_OK, "close pins");
    struct fs_information info;
    fs_check(fs_stat_path(NULL, "/missing/../DOCS", &info) == FS_NOT_FOUND &&
             fs_stat_path(NULL, "/ROOT.TXT/..", &info) == FS_NOT_DIRECTORY, "semantic traversal");
    fs_check(fs_context_chdir(&cwd, "../../..") == FS_OK, "root cannot escape");
    fs_check(fs_replace(NULL, "/FROM.TXT", "abc", 3) == FS_OK && fs_replace(NULL, "/TO.TXT", "z", 1) == FS_OK,
             "rename setup");
    unsigned calls = fs_test_backend_calls();
    fs_check(fs_rename(NULL, "/FROM.TXT", "/BAD?.TXT", false) == FS_INVALID &&
             fs_test_backend_calls() == calls, "validate both paths before backend access");
    fs_check(fs_open_flags(NULL, "/FROM.TXT", FS_WRITE, FS_OPEN_EXCLUSIVE, &file) == FS_INVALID &&
             fs_open_flags(NULL, "/FROM.TXT", FS_WRITE, FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE | FS_OPEN_TRUNCATE, &file) == FS_EXISTS,
             "exclusive create rejects before truncation");
    fs_check(fs_rename(NULL, "/FROM.TXT", "/from.txt", false) == FS_INVALID &&
             fs_rename(NULL, "/FROM.TXT", "/TO.TXT", false) == FS_EXISTS &&
             fs_rename(NULL, "/FROM.TXT", "/MISSING/../NEW.TXT", false) == FS_NOT_FOUND,
             "same identity, overwrite refusal and semantic destination traversal");
    fs_check(fs_open(NULL, "/FROM.TXT", FS_READ, &file) == FS_OK &&
             fs_rename(NULL, "/FROM.TXT", "/NEW.TXT", false) == FS_BUSY && fs_close(file) == FS_OK,
             "open source blocks rename");
    fs_check(fs_open(NULL, "/TO.TXT", FS_READ, &file) == FS_OK &&
             fs_rename(NULL, "/FROM.TXT", "/TO.TXT", true) == FS_BUSY && fs_close(file) == FS_OK,
             "open destination blocks replacement");
    fs_check(fs_rename(NULL, "/FROM.TXT", "/TO.TXT", true) == FS_OK &&
             fs_stat_path(NULL, "/TO.TXT", &info) == FS_OK && info.size == 3 && !info.read_only_volume &&
             fs_stat_path(NULL, "/FROM.TXT", &info) == FS_NOT_FOUND && fs_remove(NULL, "/TO.TXT") == FS_OK,
             "rename replaces the destination and preserves metadata");
    fs_check(fs_context_destroy(&cwd) == FS_OK && fs_stats().references == baseline.references, "context cleanup");
    fs_check(fs_open(NULL, "/", FS_READ, &file) == FS_OK && fs_unmount_disk() == FS_BUSY, "open root pins mount");
    uint64_t cursor = 0; struct fs_entry entry;
    fs_test_backend_bad_cursor = true;
    fs_check(fs_readdir(file, &cursor, &entry) == FS_IO_ERROR, "invalid backend cursor");
    fs_test_backend_bad_cursor = false;
    fs_check(fs_close(file) == FS_OK, "close root cursor");
    if (!baseline.references) {
        fs_check(fs_unmount_disk() == FS_OK, "unmount cleanup");
        fs_check(fs_open(NULL, "/", FS_READ, &file) == FS_UNAVAILABLE, "missing disk has no fallback filesystem");
    } else fs_check(fs_unmount_disk() == FS_BUSY, "boot root remains pinned");
}
