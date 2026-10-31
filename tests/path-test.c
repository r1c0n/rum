#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/path.h>
int main(void)
{
    struct fs_path path;
    assert(fs_path_parse("/", "/", &path) == FS_OK && !strcmp(path.canonical, "/"));
    assert(fs_path_parse("/", "////./../a//b/../c", &path) == FS_OK && !strcmp(path.canonical, "/A/C"));
    assert(path.input.count == 6);
    assert(fs_path_parse("/DOCS", "../../notes.txt", &path) == FS_OK && !strcmp(path.canonical, "/NOTES.TXT"));
    assert(fs_path_parse("/", "../..", &path) == FS_OK && !strcmp(path.canonical, "/"));
    assert(fs_path_parse("/", "/disk", &path) == FS_OK && !strcmp(path.canonical, "/DISK"));
    assert(fs_path_parse("/", "/rum/shell.elf", &path) == FS_OK && !strcmp(path.canonical, "/RUM/SHELL.ELF"));
    assert(fs_path_parse("/", "abcdefgh.txt", &path) == FS_OK);
    assert(fs_path_parse("/", "abcdefghi.txt", &path) == FS_NAME_TOO_LONG);
    assert(fs_path_parse("/", "a.abcd", &path) == FS_NAME_TOO_LONG);
    assert(fs_path_parse("/", "a.b.c", &path) == FS_INVALID);
    assert(fs_path_parse("/", "bad name", &path) == FS_INVALID);
    assert(fs_path_parse("relative", "a", &path) == FS_INVALID);
    char deep[FS_PATH_CAPACITY] = "/";
    for (unsigned i = 0; i < FS_PATH_DEPTH; ++i) strcat(deep, "A/");
    assert(fs_path_parse("/", deep, &path) == FS_OK && path.depth == FS_PATH_DEPTH);
    strcat(deep, "B"); assert(fs_path_parse("/", deep, &path) == FS_TOO_DEEP);
    char overlong[FS_PATH_CAPACITY + 1]; memset(overlong, 'A', sizeof overlong); overlong[sizeof overlong - 1] = 0;
    assert(fs_path_parse("/", overlong, &path) == FS_PATH_TOO_LONG);
    puts("PASS: FAT root paths, normalization, bounded traversal and 8.3 limits");
    return 0;
}
