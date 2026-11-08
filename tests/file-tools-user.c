#include <rum/tools.h>
static unsigned char pages[3 * 4096] __attribute__((aligned(4096)));
static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void check(int ok, int code) { if (!ok) { tool_print("file tools check failed: "); tool_number(code); tool_print("\n"); rum_exit(code); } }
static rum_result_t raw(uint32_t op, void *packet) { return rum_syscall3(op, (rum_address_t)(uintptr_t)packet, 0, 0); }
int main(int argc, char **argv)
{
    if (argc == 2 && equal(argv[1], "hold")) {
        rum_result_t fd = rum_open("/TARGET.BIN", RUM_OPEN_READ, 0); check(fd >= 3, 1);
        tool_print("file pin ready\n"); (void)rum_sleep(RUM_SLEEP_MAX_MS); (void)rum_close((rum_handle_t)fd); return 0;
    }
    if (argc != 2 || !equal(argv[1], "validate")) return 1;
    struct rum_file_information info;
    struct rum_stat_request request = {.version = 1, .size = sizeof request,
        .path = (rum_address_t)(uintptr_t)"/TARGET.BIN", .data = (rum_address_t)(uintptr_t)&info};
    check(raw(RUM_SYS_STAT, (void *)UINT32_MAX) == -RUM_EFAULT &&
          rum_syscall3(RUM_SYS_STAT, (rum_address_t)(uintptr_t)&request, 1, 0) == -RUM_EINVAL, 2);
    request.version = 2; check(raw(RUM_SYS_STAT, &request) == -RUM_EINVAL, 3); request.version = 1;
    request.size--; check(raw(RUM_SYS_STAT, &request) == -RUM_EINVAL, 4); request.size++;
    request.flags = 2; check(raw(RUM_SYS_STAT, &request) == -RUM_EINVAL, 5); request.flags = 0;
    request.reserved[1] = 1; check(raw(RUM_SYS_STAT, &request) == -RUM_EINVAL, 6); request.reserved[1] = 0;
    request.handle = 3; check(raw(RUM_SYS_STAT, &request) == -RUM_EINVAL, 7); request.handle = 0;
    request.data = (rum_address_t)(uintptr_t)&request; check(raw(RUM_SYS_STAT, &request) == -RUM_EFAULT, 8);
    request.data = UINT32_MAX; check(raw(RUM_SYS_STAT, &request) == -RUM_EFAULT, 9);
    request.data = (rum_address_t)(uintptr_t)"readonly"; check(raw(RUM_SYS_STAT, &request) == -RUM_EFAULT, 10);
    struct rum_stat_request *cross = (void *)(pages + 4096 - 8);
    *cross = (struct rum_stat_request){.version = 1, .size = sizeof *cross,
        .path = (rum_address_t)(uintptr_t)"/TARGET.BIN", .data = (rum_address_t)(uintptr_t)(pages + 8192 - 16)};
    check(!raw(RUM_SYS_STAT, cross), 11);
    struct rum_file_information *out = (void *)(pages + 8192 - 16);
    check(out->version == 1 && out->size == sizeof *out && out->kind == RUM_ENTRY_FILE && out->size_lo == 1537 &&
          !out->size_hi && out->volume && out->generation && !out->reserved[0] && !out->reserved[1], 12);
    for (unsigned i = 0; i < sizeof info; ++i) ((unsigned char *)&info)[i] = 0xa5;
    check(rum_stat("/MISSING.BIN", &info) == -RUM_ENOENT, 13);
    for (unsigned i = 0; i < sizeof info; ++i) check(((unsigned char *)&info)[i] == 0xa5, 14);
    check(rum_fstat(UINT32_MAX, &info) == -RUM_EBADF && rum_fstat(0, &info) == -RUM_EBADF, 15);
    rum_result_t file = rum_open("/TARGET.BIN", RUM_OPEN_READ, 0); check(file >= 3 && !rum_fstat((rum_handle_t)file, &info), 16);
    check(info.object_lo == out->object_lo && info.object_hi == out->object_hi && info.volume == out->volume, 17);
    uint64_t offset;
    check(!rum_seek((rum_handle_t)file, 0, RUM_SEEK_CUR, &offset) && !offset, 18);
    check(rum_rename("/TARGET.BIN", "/NEW.BIN", 0) == -RUM_EBUSY && !rum_close((rum_handle_t)file), 19);
    check(rum_open("/TARGET.BIN", RUM_OPEN_WRITE, RUM_OPEN_EXCLUSIVE) == -RUM_EINVAL &&
          rum_open("/TARGET.BIN", RUM_OPEN_WRITE, RUM_OPEN_CREATE | RUM_OPEN_EXCLUSIVE | RUM_OPEN_TRUNCATE) == -RUM_EEXIST, 20);
    check(rum_rename("/TARGET.BIN", "/KEEP.BIN", 0) == -RUM_EEXIST &&
          rum_rename("/TARGET.BIN", "/target.bin", 0) == -RUM_EINVAL &&
          rum_rename("/TARGET.BIN", "/VOID", RUM_RENAME_REPLACE) == -RUM_EISDIR, 21);
    file = rum_open("/KEEP.BIN", RUM_OPEN_READ, 0);
    check(file >= 3 && rum_rename("/TARGET.BIN", "/KEEP.BIN", RUM_RENAME_REPLACE) == -RUM_EBUSY && !rum_close((rum_handle_t)file), 22);
    struct rum_rename_request rename = {1, sizeof rename, (rum_address_t)(uintptr_t)"/TARGET.BIN",
        (rum_address_t)(uintptr_t)"/NEW.BIN", 2, 0};
    check(raw(RUM_SYS_RENAME, &rename) == -RUM_EINVAL, 23); rename.flags = 0;
    rename.reserved = 1; check(raw(RUM_SYS_RENAME, &rename) == -RUM_EINVAL, 24); rename.reserved = 0;
    rename.version = 2; check(raw(RUM_SYS_RENAME, &rename) == -RUM_EINVAL, 25); rename.version = 1;
    rename.size--; check(raw(RUM_SYS_RENAME, &rename) == -RUM_EINVAL, 26); rename.size++;
    rename.destination = UINT32_MAX; check(raw(RUM_SYS_RENAME, &rename) == -RUM_EFAULT, 27);
    check(rum_rename("/TARGET.BIN", "/BAD?.TXT", 0) == -RUM_EINVAL &&
          rum_rename("/TARGET.BIN", "/MISSING/../NEW.BIN", 0) == -RUM_ENOENT &&
          rum_rename("/VOID", "/NEW", 0) == -RUM_EISDIR, 28);
    struct rum_rename_request *cross_rename = (void *)(pages + 4096 - 12);
    *cross_rename = (struct rum_rename_request){1, sizeof *cross_rename, (rum_address_t)(uintptr_t)"/MISSING.BIN",
        (rum_address_t)(uintptr_t)"/NEW.BIN", 0, 0};
    check(raw(RUM_SYS_RENAME, cross_rename) == -RUM_ENOENT &&
          rum_syscall3(RUM_SYS_RENAME, (rum_address_t)(uintptr_t)cross_rename, 0, 1) == -RUM_EINVAL, 29);
    for (unsigned i = 0; i < 64; ++i) check(!rum_stat("/", &info) && info.kind == RUM_ENTRY_DIRECTORY, 30);
    tool_print("file metadata and rename ABI verified\n"); return 0;
}
