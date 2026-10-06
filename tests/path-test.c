#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/path.h>

int main(void)
{
    struct fs_path path;
    assert(fs_path_parse("/", "/", &path) == FS_OK && !strcmp(path.canonical, "/"));
    assert(fs_path_parse("/", "////./../a//b/../c", &path) == FS_OK && !strcmp(path.canonical, "/a/c"));
    assert(path.input.count == 6); /* Preserve b/.. for semantic lookup. */
    assert(fs_path_parse("/disk/dir", "../test.txt", &path) == FS_OK &&
           !strcmp(path.canonical, "/disk/TEST.TXT") && path.mount == FS_MOUNT_DISK);
    assert(fs_path_parse("/disk", "..", &path) == FS_MOUNT_ESCAPE);
    assert(fs_path_parse("/", "/disk/dir/../../ram", &path) == FS_MOUNT_ESCAPE);
    assert(fs_path_parse("/disk/dir", "/readme.txt", &path) == FS_OK &&
           !strcmp(path.canonical, "/readme.txt") && path.mount == FS_MOUNT_RAM);
    assert(fs_path_parse("/", "/disk/foo.txt/", &path) == FS_OK && path.input.directory);
    assert(fs_path_parse("/", "/file/.", &path) == FS_OK && path.input.directory);
    for (const char **name = (const char *[]){"", "a b", "a\\b", "a:b", "\177", "\200", "a\n", NULL}; *name; ++name)
        assert(fs_path_parse("/", *name, &path) == FS_INVALID);
    assert(fs_path_parse(NULL, "a", &path) == FS_INVALID);
    assert(fs_path_parse("relative", "a", &path) == FS_INVALID);
    assert(fs_path_parse("/", "a", NULL) == FS_INVALID);
    char name[65]; memset(name, 'a', 63); name[63] = 0;
    assert(fs_path_parse("/", name, &path) == FS_OK);
    name[63] = 'a'; name[64] = 0;
    assert(fs_path_parse("/", name, &path) == FS_NAME_TOO_LONG);
    assert(fs_path_parse("/disk", "12345678.xyz", &path) == FS_OK);
    assert(fs_path_parse("/disk", "123456789", &path) == FS_NAME_TOO_LONG);
    assert(fs_path_parse("/disk", "a.abcd", &path) == FS_NAME_TOO_LONG);
    assert(fs_path_parse("/disk", ".a", &path) == FS_INVALID);
    assert(fs_path_parse("/disk", "a.", &path) == FS_INVALID);
    assert(fs_path_parse("/disk", "a.b.c", &path) == FS_INVALID);
    char long_path[257]; memset(long_path, '/', 255); long_path[255] = 0;
    assert(fs_path_parse("/", long_path, &path) == FS_OK);
    long_path[255] = '/'; long_path[256] = 0;
    assert(fs_path_parse("/", long_path, &path) == FS_PATH_TOO_LONG);
    char deep[64] = "/disk";
    for (unsigned i = 0; i < FS_PATH_DEPTH; ++i) strcat(deep, "/a");
    assert(fs_path_parse("/", deep, &path) == FS_OK && path.depth == FS_PATH_DEPTH);
    strcat(deep, "/a/..");
    assert(fs_path_parse("/", deep, &path) == FS_TOO_DEEP);
    char base[256]; memset(base, 'a', sizeof(base));
    assert(fs_path_parse(base, "x", &path) == FS_PATH_TOO_LONG);
    assert(fs_path_parse("/", "/disk/invalid-name/..", &path) == FS_NAME_TOO_LONG);
    /* Raw arguments fit, but their resolved relative path does not. */
    char parent[256] = "/", suffix[65];
    memset(suffix, 'b', 63); suffix[63] = 0;
    for (unsigned i = 0; i < 3; ++i) { strcat(parent, suffix); strcat(parent, "/"); }
    assert(fs_path_parse(parent, suffix, &path) == FS_PATH_TOO_LONG);
    for (enum block_error e = BLOCK_OK; e <= BLOCK_UNSUPPORTED; ++e)
        assert(fs_error_from_block(e) != FS_END);
    assert(fs_error_from_block(BLOCK_TIMEOUT) == FS_TIMEOUT);
    puts("path parser tests passed");
}
