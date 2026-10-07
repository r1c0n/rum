#include <stdio.h>
#include <stdlib.h>
#include <rum/fs.h>
#include <rum/heap.h>
#include <rum/ramfs.h>
#include "page-backend.h"
#include "fs-checks.h"

void fs_check(bool condition, const char *name)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); abort(); }
}
int main(void)
{
    test_page_budget(-1);
    fs_check(heap_initialize() && ramfs_initialize() && fs_initialize(), "filesystem setup");
    fs_checks();
    puts("PASS: filesystem paths, mounts, directories, binary I/O, access modes, retention, bounds and cleanup");
    return 0;
}
