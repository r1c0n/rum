#include <rum/user.h>
#include <rum/abi/layout.h>

#define PAGE 0x80800000u
static void check(int ok, int code)
{
    if (!ok) rum_exit(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static rum_result_t raw_open(struct rum_open_request *request)
{
    return rum_syscall3(RUM_SYS_OPEN, (rum_address_t)(uintptr_t)request, 0, 0);
}
static void invalid_calls(rum_handle_t file)
{
    char buffer[32];
    check(rum_read(32, buffer, 1) == -RUM_EBADF && rum_write(UINT32_MAX, buffer, 1) == -RUM_EBADF &&
          rum_close(UINT32_MAX) == -RUM_EBADF && rum_flush(0) == -RUM_EBADF, 10);
    struct rum_open_request request = { RUM_FS_ABI_VERSION, sizeof request, (rum_address_t)(uintptr_t)"/data.bin", RUM_OPEN_READ, 0, 0 };
    request.version = 2; check(raw_open(&request) == -RUM_EINVAL, 11);
    request.version = 1; request.size--; check(raw_open(&request) == -RUM_EINVAL, 12);
    request.size++; request.reserved = 1; check(raw_open(&request) == -RUM_EINVAL, 13);
    request.reserved = 0; request.access = 0; check(raw_open(&request) == -RUM_EINVAL, 14);
    request.access = 4; check(raw_open(&request) == -RUM_EINVAL, 15);
    request.access = 1; request.flags = 2; check(raw_open(&request) == -RUM_EINVAL, 16);
    check(rum_open("/data.bin", 2, RUM_OPEN_DIRECTORY) == -RUM_EINVAL &&
          rum_open("/data.bin", 1, RUM_OPEN_DIRECTORY) == -RUM_ENOTDIR, 17);
    check(rum_open("/", 2, 0) == -RUM_EISDIR && rum_open("/absent", 1, 0) == -RUM_ENOENT &&
          rum_open("bad:name", 1, 0) == -RUM_EINVAL, 18);
    check(rum_open((const char *)UINT32_MAX, 1, 0) == -RUM_EFAULT &&
          rum_syscall3(RUM_SYS_OPEN, PAGE + 4 * 4096 - 12, 0, 0) == -RUM_EFAULT, 19);
    char *long_path = (char *)PAGE;
    for (unsigned i = 0; i < RUM_ABI_PATH_CAPACITY; ++i) long_path[i] = 'a';
    check(rum_open(long_path, 1, 0) == -RUM_ENAMETOOLONG, 20);
    long_path[64] = 0; check(rum_open(long_path, 1, 0) == -RUM_ENAMETOOLONG, 21);
    check(rum_write(file, (void *)(PAGE + 3 * 4096 - 600), 700) == -RUM_EFAULT &&
          rum_read(file, (void *)(PAGE + 2 * 4096 - 64), 192) == -RUM_EFAULT &&
          rum_read(file, (void *)(RUM_ABI_STACK_TOP - 4), 8) == -RUM_EFAULT &&
          rum_write(file, (void *)(PAGE + 4 * 4096), 1) == -RUM_EFAULT, 22);
    check(rum_read(file, (void *)UINT32_MAX, 0) == 0 && rum_write(file, (void *)UINT32_MAX, 0) == 0, 23);
    struct rum_seek_request seek = {1, sizeof seek, 0, 0, 0, 1, 0, 0};
    check(rum_syscall3(RUM_SYS_SEEK, file, (rum_address_t)(uintptr_t)&seek, 0) == -RUM_EINVAL &&
          rum_syscall3(RUM_SYS_SEEK, file, PAGE + 2 * 4096, 0) == -RUM_EFAULT, 24);
    seek.reserved = 0; seek.position_hi = 1;
    check(rum_syscall3(RUM_SYS_SEEK, file, (rum_address_t)(uintptr_t)&seek, 0) == -RUM_EINVAL, 37);
    seek.position_hi = 0;
    struct rum_seek_request *cross_seek = (void *)(PAGE + 4096 - 12);
    *cross_seek = seek;
    check(rum_syscall3(RUM_SYS_SEEK, file, (rum_address_t)(uintptr_t)cross_seek, 0) == 0 &&
          !cross_seek->position_lo && !cross_seek->position_hi, 38);
    check(rum_getcwd(buffer, 1) == -RUM_ERANGE && rum_getcwd((char *)UINT32_MAX, 0) == -RUM_ERANGE &&
          rum_getcwd((char *)(PAGE + 3 * 4096 - 600), 700) == -RUM_EFAULT, 25);
    rum_result_t other = rum_open("/data.bin", RUM_OPEN_READ, 0);
    check(other == 4 && rum_write((rum_handle_t)other, buffer, 0) == -RUM_EBADF && rum_close((rum_handle_t)other) == 0, 26);
    other = rum_open("/data.bin", RUM_OPEN_WRITE, 0);
    check(other == 4 && rum_read((rum_handle_t)other, buffer, 1) == -RUM_EBADF && rum_close((rum_handle_t)other) == 0, 27);
    char *path = (char *)(PAGE + 4096 - 5);
    static const char name[] = "/data.bin";
    for (unsigned i = 0; i < sizeof name; ++i) path[i] = name[i];
    request = (struct rum_open_request){1, sizeof request, (rum_address_t)(uintptr_t)path, 1, 0, 0};
    struct rum_open_request *cross = (void *)(PAGE + 4096 - 12);
    /* Keep the path away from the packet while both cross page boundaries. */
    other = raw_open(&request);
    check(other == 4 && rum_close((rum_handle_t)other) == 0, 28);
    request.path = (rum_address_t)(uintptr_t)name; *cross = request;
    other = raw_open(cross);
    check(other == 4 && rum_close((rum_handle_t)other) == 0, 29);
}
static void offsets(rum_handle_t file)
{
    uint64_t position;
    char byte;
    check(rum_seek(file, -1, RUM_SEEK_SET, &position) == -RUM_EOVERFLOW &&
          rum_seek(file, 0, 3, &position) == -RUM_EINVAL && rum_seek(1, 0, 0, &position) == -RUM_EINVAL, 30);
    check(rum_seek(file, INT64_MAX, 0, &position) == 0 && position == INT64_MAX && rum_read(file, &byte, 1) == 0, 31);
    check(rum_seek(file, INT64_MAX, 1, &position) == 0 && position == UINT64_MAX - 1 &&
          rum_seek(file, 1, 1, &position) == 0 && position == UINT64_MAX, 32);
    check(rum_seek(file, 1, 1, &position) == -RUM_EOVERFLOW && rum_read(file, &byte, 1) == -RUM_EOVERFLOW &&
          rum_write(file, &byte, 1) == -RUM_EOVERFLOW, 33);
    check(rum_seek(file, -600, 1, &position) == 0 && rum_write(file, (void *)PAGE, 700) == -RUM_EOVERFLOW &&
          rum_seek(file, 600, 1, &position) == 0, 36);
    check(rum_seek(file, INT64_MIN, 1, &position) == 0 && position == INT64_MAX, 34);
    check(rum_seek(file, 0, 0, &position) == 0 && position == 0, 35);
}
static void directory_tests(void)
{
    struct rum_directory_entry entry = { .version = 1, .size = sizeof entry };
    rum_handle_t directory = (rum_handle_t)rum_open("/", 1, RUM_OPEN_DIRECTORY);
    check(directory < 32 && rum_read(directory, &entry, 1) == -RUM_EISDIR && rum_seek(directory, 1, 0, 0) == -RUM_EINVAL, 40);
    entry.reserved = 1;
    check(rum_syscall3(RUM_SYS_READDIR, directory, (rum_address_t)(uintptr_t)&entry, 0) == -RUM_EINVAL, 41);
    check(rum_syscall3(RUM_SYS_READDIR, directory, PAGE + 2 * 4096 - 16, 0) == -RUM_EFAULT, 42);
    check(rum_readdir(directory, 0) == -RUM_EFAULT, 49);
    entry.reserved = 0;
    check(rum_readdir(directory, &entry) == 1 && equal(entry.name, "disk") && entry.kind == RUM_ENTRY_DIRECTORY, 43);
    struct rum_directory_entry *cross = (void *)(PAGE + 4096 - 16);
    *cross = (struct rum_directory_entry){ .version = 1, .size = sizeof *cross };
    check(rum_readdir(directory, cross) == 1 && cross->kind == RUM_ENTRY_FILE && equal(cross->name, "data.bin"), 44);
    check(rum_seek(directory, 0, 0, 0) == 0 && rum_readdir(directory, &entry) == 1 && equal(entry.name, "disk"), 45);
    unsigned count = 0;
    rum_result_t result;
    while ((result = rum_readdir(directory, &entry)) == 1) check(++count < 64, 46);
    check(result == 0 && rum_readdir(directory, &entry) == 0 && !entry.kind && !entry.name[0], 47);
    check(rum_close(directory) == 0 && rum_close(directory) == -RUM_EBADF, 48);
}
static void exhaust(void)
{
    rum_handle_t handles[29];
    for (unsigned i = 0; i < 29; ++i) {
        rum_result_t handle = rum_open("/data.bin", 1, 0);
        check(handle == (rum_result_t)i + 3, 50); handles[i] = (rum_handle_t)handle;
    }
    check(rum_open("/data.bin", 1, 0) == -RUM_EMFILE, 51);
    check(rum_close(0) == 0 && rum_close(1) == 0 && rum_close(2) == 0 && rum_open("/data.bin", 1, 0) == -RUM_EMFILE, 52);
    char byte;
    check(rum_read(0, &byte, 1) == -RUM_EBADF && rum_write(1, &byte, 0) == -RUM_EBADF, 53);
    check(rum_close(handles[13]) == 0 && rum_open("/data.bin", 1, 0) == (rum_result_t)handles[13], 54);
    /* Deliberately leave all 29 references open for exit cleanup. */
}
int main(int argc, char **argv)
{
    check(argc == 2, 1);
    char mode = argv[1][0], buffer[32];
    rum_result_t result = rum_open("/data.bin", 3, 0);
    check(result == 3, 2);
    rum_handle_t file = (rum_handle_t)result;
    if (mode == 'f' || mode == 'c') check(rum_open("/disk/DOCS/README.TXT", 1, 0) == 4, 3);
    if (mode == 'f') { __asm__ volatile("ud2"); }
    if (mode == 'c') { rum_read(0, buffer, sizeof buffer); return 99; }
    if (mode == 's') {
        rum_result_t disk = rum_open("/disk/DOCS/NOTE.TXT", 3, 0);
        check(disk == 4 && rum_read((rum_handle_t)disk, buffer, sizeof buffer) == 7, 60);
        check(rum_read((rum_handle_t)disk, buffer, sizeof buffer) == -RUM_EIO, 61);
        check(rum_seek((rum_handle_t)disk, 0, 0, 0) == 0 && rum_write((rum_handle_t)disk, "pattern", 7) == 7, 62);
        check(rum_write((rum_handle_t)disk, "pattern", 7) == -RUM_EIO, 63);
        return 0;
    }
    if (mode == 'd' || mode == 'r') {
        check(rum_chdir("/disk/DOCS") == 0 && rum_getcwd(buffer, sizeof buffer) == 11 && equal(buffer, "/disk/DOCS"), 70);
        check(rum_chdir("../..") == -RUM_EINVAL && rum_open("../..", 1, 0) == -RUM_EINVAL, 71);
        rum_result_t directory = rum_open(".", 1, RUM_OPEN_DIRECTORY);
        struct rum_directory_entry entry = { .version = 1, .size = sizeof entry };
        check(directory == 4 && rum_readdir((rum_handle_t)directory, &entry) == 1 &&
              equal(entry.name, "NOTE.TXT") && entry.kind == RUM_ENTRY_FILE && entry.size_lo == 13 &&
              rum_readdir((rum_handle_t)directory, &entry) == 0 && rum_close((rum_handle_t)directory) == 0, 79);
        rum_result_t disk = rum_open("note.txt", 1, 0);
        check(disk == 4 && rum_read((rum_handle_t)disk, buffer, sizeof buffer) == 13 && equal(buffer, "persistent\r\n"), 72);
        check(rum_close((rum_handle_t)disk) == 0, 73);
        if (mode == 'r') {
            check(rum_open("note.txt", 2, 0) == -RUM_EROFS && rum_mkdir("CHILD") == -RUM_EROFS, 74);
        } else {
            disk = rum_open("NOTE.TXT", 3, 0);
            check(disk == 4 && rum_write((rum_handle_t)disk, "userspace", 9) == 9 && rum_flush((rum_handle_t)disk) == 0, 75);
            check(rum_remove("NOTE.TXT") == -RUM_EBUSY && rum_close((rum_handle_t)disk) == 0, 76);
            check(rum_mkdir("CHILD") == 0 && rum_chdir("CHILD") == 0 && rum_remove("../CHILD") == -RUM_EBUSY, 77);
            check(rum_chdir("..") == 0 && rum_remove("CHILD") == 0, 78);
        }
        return 0;
    }
    invalid_calls(file); offsets(file); directory_tests();
    char *cross = (char *)(PAGE + 4096 - 64);
    check(rum_read(file, cross, 700) == 512, 80);
    for (unsigned i = 0; i < 512; ++i) check((unsigned char)cross[i] == (unsigned char)(i * 17 + 3), 81);
    check(rum_seek(file, 0, 0, 0) == 0, 82);
    for (unsigned i = 0; i < 700; ++i) cross[i] = (char)(i ^ 0xa5);
    check(rum_write(file, cross, 700) == 512 && rum_write(file, cross + 512, 188) == 188, 83);
    check(rum_seek(file, -4, RUM_SEEK_END, 0) == 0 && rum_read(file, buffer, sizeof buffer) == 4 && rum_read(file, buffer, 1) == 0, 84);
    check(rum_remove("/data.bin") == -RUM_EBUSY && rum_close(file) == 0 && rum_read(file, buffer, 1) == -RUM_EBADF, 85);
    exhaust();
    return 0;
}
