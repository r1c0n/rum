#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>
#include <rum/user.h>

static struct { void *pointer; uint32_t bytes; } mappings[64];
static size_t mapped, budget = 16 * 1024 * 1024;
static unsigned allocations, releases, release_failure;
rum_result_t rum_vm_allocate(struct rum_vm_allocate_request *request)
{
    ++allocations;
    if (request->bytes > budget - mapped) return -RUM_ENOMEM;
    unsigned i; for (i = 0; i < 64; ++i) if (!mappings[i].pointer) break;
    if (i == 64) return -RUM_ENOMEM;
    void *pointer = mmap(NULL, request->bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(pointer != MAP_FAILED && (uintptr_t)pointer <= UINT32_MAX);
    mappings[i].pointer = pointer; mappings[i].bytes = request->bytes;
    mapped += request->bytes; request->address = (uint32_t)(uintptr_t)pointer; return 0;
}
rum_result_t rum_vm_release(rum_address_t address, rum_size_t bytes)
{
    if (release_failure) { --release_failure; return -RUM_EBUSY; }
    for (unsigned i = 0; i < 64; ++i) if ((uintptr_t)mappings[i].pointer == address) {
        assert(mappings[i].bytes == bytes && !munmap(mappings[i].pointer, bytes));
        mappings[i].pointer = NULL; mapped -= bytes; ++releases; return 0;
    }
    assert(0 && "release must own the exact region"); return -RUM_EINVAL;
}
static unsigned char disk[16384];
static size_t disk_bytes;
static struct { unsigned used, access, append; uint64_t offset; } descriptors[32];
static int exists, readonly, read_error, write_error, flush_error, close_error;
static unsigned read_calls, write_calls, flushes;
static size_t write_fail_at = SIZE_MAX;
rum_result_t rum_open(const char *path, uint32_t access, uint32_t flags)
{
    assert(!strcmp(path, "FILE.TXT"));
    if (readonly && (access & RUM_OPEN_WRITE)) return -RUM_EROFS;
    if (!exists && !(flags & RUM_OPEN_CREATE)) return -RUM_ENOENT;
    unsigned i; for (i = 3; i < 32; ++i) if (!descriptors[i].used) break;
    if (i == 32) return -RUM_EMFILE;
    exists = 1; if (flags & RUM_OPEN_TRUNCATE) disk_bytes = 0;
    descriptors[i].used = 1; descriptors[i].access = access;
    descriptors[i].append = flags & RUM_OPEN_APPEND; descriptors[i].offset = 0;
    return (rum_result_t)i;
}
rum_result_t rum_close(rum_handle_t fd)
{
    assert(fd >= 3 && fd < 32 && descriptors[fd].used);
    descriptors[fd].used = 0; return close_error ? -RUM_EIO : 0;
}
rum_result_t rum_fstat(rum_handle_t fd, struct rum_file_information *info)
{
    (void)info; return fd < 3 ? -RUM_EBADF : 0;
}
rum_result_t rum_flush(rum_handle_t fd) { assert(fd >= 3 && descriptors[fd].used); ++flushes; return flush_error ? -RUM_EIO : 0; }
rum_result_t rum_read(rum_handle_t fd, void *out, rum_size_t bytes)
{
    ++read_calls; assert(fd >= 3 && descriptors[fd].used && (descriptors[fd].access & RUM_OPEN_READ));
    if (read_error) return -RUM_EIO;
    uint64_t offset = descriptors[fd].offset;
    size_t available = offset >= disk_bytes ? 0 : disk_bytes - (size_t)offset;
    if (bytes > 7) bytes = 7;
    if (bytes > available) bytes = (uint32_t)available;
    memcpy(out, disk + (size_t)(offset < sizeof disk ? offset : 0), bytes); descriptors[fd].offset += bytes;
    return (rum_result_t)bytes;
}
rum_result_t rum_write(rum_handle_t fd, const void *in, rum_size_t bytes)
{
    ++write_calls; assert(fd >= 3 && descriptors[fd].used && (descriptors[fd].access & RUM_OPEN_WRITE));
    if (write_error) return -RUM_ENOSPC;
    if (descriptors[fd].append) descriptors[fd].offset = disk_bytes;
    uint64_t offset = descriptors[fd].offset;
    if (offset >= sizeof disk) return -RUM_ENOSPC;
    if (offset >= write_fail_at) return -RUM_ENOSPC;
    if (bytes > 5) bytes = 5;
    if (bytes > write_fail_at - offset) bytes = (uint32_t)(write_fail_at - offset);
    if (bytes > sizeof disk - offset) bytes = (uint32_t)(sizeof disk - offset);
    memcpy(disk + offset, in, bytes); descriptors[fd].offset += bytes;
    if (descriptors[fd].offset > disk_bytes) disk_bytes = (size_t)descriptors[fd].offset;
    return (rum_result_t)bytes;
}
rum_result_t rum_seek(rum_handle_t fd, int64_t offset, uint32_t whence, uint64_t *position)
{
    assert(fd >= 3 && descriptors[fd].used);
    int64_t base = whence == RUM_SEEK_SET ? 0 : whence == RUM_SEEK_CUR ? (int64_t)descriptors[fd].offset : (int64_t)disk_bytes;
    if (offset < -base) return -RUM_EINVAL;
    *position = descriptors[fd].offset = (uint64_t)(base + offset); return 0;
}
rum_result_t rum_remove(const char *path) { (void)path; return -RUM_EBUSY; }
rum_result_t rum_rename(const char *a, const char *b, uint32_t flags) { (void)a; (void)b; assert(flags == RUM_RENAME_REPLACE); return -RUM_EACCES; }
_Noreturn void rum_exit(int32_t status) { _exit(status); }

static void heap_checks(void)
{
    errno = 41; assert(!malloc(0) && !calloc(0, SIZE_MAX) && errno == 41);
    assert(!calloc(SIZE_MAX / 2 + 1, 2) && errno == ENOMEM);
    assert(!malloc(SIZE_MAX) && errno == ENOMEM);
    void *slots[400];
    for (unsigned i = 0; i < 400; ++i) {
        size_t bytes = i * 7 + 1; slots[i] = malloc(bytes);
        assert(slots[i] && !((uintptr_t)slots[i] & 15)); memset(slots[i], (int)(i % 251), bytes);
    }
    for (unsigned i = 0; i < 400; i += 2) free(slots[i]);
    for (unsigned i = 1; i < 400; i += 2) {
        for (unsigned j = 0; j < i * 7 + 1; ++j) assert(((unsigned char *)slots[i])[j] == i % 251);
        free(slots[i]);
    }
    assert(!mapped && releases);
    unsigned char *a = malloc(128), *b = malloc(128), *guard = malloc(128);
    assert(a && b && guard); memset(a, 0x51, 128); free(b);
    assert(realloc(a, 240) == a); assert(realloc(a, 32) == a);
    unsigned char *moved = realloc(a, 128 * 1024);
    assert(moved); for (unsigned i = 0; i < 32; ++i) assert(moved[i] == 0x51);
    budget = mapped;
    assert(!realloc(moved, 8 * 1024 * 1024) && errno == ENOMEM);
    for (unsigned i = 0; i < 32; ++i) assert(moved[i] == 0x51);
    budget = 16 * 1024 * 1024; free(moved); free(guard); assert(!mapped);
    a = malloc(256); guard = malloc(256); memset(a, 0xff, 256); free(a);
    a = calloc(256, 1); assert(a); for (unsigned i = 0; i < 256; ++i) assert(!a[i]);
    free(guard); assert(!realloc(a, 0) && !mapped);
    release_failure = 1; a = malloc(1); unsigned calls = allocations; free(a);
    assert(mapped); a = malloc(1); assert(allocations == calls); free(a); assert(!mapped);
    budget = 4096; a = malloc(1); assert(a && mapped == 4096); free(a); budget = 16 * 1024 * 1024;
    for (unsigned i = 0; i < 120; ++i) {
        size_t n = 65537 + i; a = calloc(n, 1); assert(a); a[n - 1] = 0xcc; free(a);
    }
    assert(!mapped);
    struct { unsigned char *data; size_t bytes; unsigned char tag; } fragments[128] = {0};
    uint32_t random = 0x726d1234u;
    for (unsigned turn = 0; turn < 4000; ++turn) {
        random = random * 1664525u + 1013904223u;
        unsigned index = (random >> 16) % 128;
        unsigned char *old = fragments[index].data;
        size_t old_bytes = fragments[index].bytes;
        for (size_t i = 0; i < old_bytes; ++i) assert(old[i] == fragments[index].tag);
        size_t bytes = (random >> 3) % 20000;
        unsigned char *replacement = realloc(old, bytes);
        assert(replacement || !bytes);
        for (size_t i = 0; i < old_bytes && i < bytes; ++i) assert(replacement[i] == fragments[index].tag);
        fragments[index].data = replacement; fragments[index].bytes = bytes;
        fragments[index].tag = (unsigned char)(turn % 251);
        memset(replacement, fragments[index].tag, bytes);
    }
    for (unsigned i = 0; i < 128; ++i) free(fragments[i].data);
    assert(!mapped);
}
static void string_checks(void)
{
    char text[32] = "abcdefgh";
    memmove(text + 2, text, 8); assert(!memcmp(text, "ababcdefgh", 10));
    memmove(text, text + 2, 8); assert(!memcmp(text, "abcdefgh", 8));
    strcpy(text, "rum"); strcat(text, " OS"); assert(!strcmp(text, "rum OS"));
    strncpy(text, "a", 8); assert(text[0] == 'a' && !text[7]);
    assert(memcmp("\xff", "\x01", 1) > 0 && strncmp("a", "b", 1) < 0 && !strncmp("a", "b", 0));
    assert(strchr("rum", 0) && !strchr("rum", 'x') && strrchr("rum rum", 'r'));
    assert(strstr("rum OS", "OS") && !strstr("rum", "rums") && memchr("\0a", 'a', 2));
    char *copy = strdup("runtime"); assert(copy && !strcmp(copy, "runtime")); free(copy); assert(!mapped);
}
static void io_checks(void)
{
    assert(!fopen("FILE.TXT", "rb") && errno == ENOENT);
    const char *bad[] = { "", "q", "rbb", "r++", "wX", "r+b+" };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) assert(!fopen("FILE.TXT", bad[i]) && errno == EINVAL);
    FILE *f = fopen("FILE.TXT", "w+b"); assert(f);
    const char data[] = "alpha\0beta\r\nlast";
    assert(fwrite(data, 1, sizeof data, f) == sizeof data && write_calls > 1 && ftell(f) == sizeof data);
    assert(!fseek(f, 0, SEEK_SET)); char out[64];
    assert(fread(out, 2, 30, f) == sizeof data / 2 && feof(f) && !ferror(f) && !memcmp(out, data, sizeof data));
    rewind(f); assert(!feof(f) && !ferror(f));
    assert(fread(out, 1, 5, f) == 5 && !feof(f));
    unsigned reads = read_calls;
    assert(!fread(out, SIZE_MAX, 2, f) && errno == EOVERFLOW && ferror(f) && reads == read_calls);
    clearerr(f); read_error = 1; assert(!fread(out, 1, 1, f) && ferror(f) && !feof(f)); read_error = 0;
    rewind(f); write_error = 1; assert(!fwrite("x", 1, 1, f) && errno == ENOSPC); write_error = 0;
    clearerr(f); flush_error = 1; assert(fflush(f) == EOF && errno == EIO && ferror(f)); flush_error = 0;
    assert(!fclose(f) && flushes);
    f = fopen("FILE.TXT", "a+"); assert(f && !fseek(f, 0, SEEK_SET));
    assert(fwrite("Z", 1, 1, f) == 1 && disk[disk_bytes - 1] == 'Z' && !fclose(f));
    f = fopen("FILE.TXT", "r"); assert(f);
    assert(!fwrite("x", 1, 1, f) && ferror(f) && errno == EBADF); assert(!fclose(f));
    readonly = 1; assert(!fopen("FILE.TXT", "w") && errno == EROFS); readonly = 0;
    FILE *all[29]; for (unsigned i = 0; i < 29; ++i) assert((all[i] = fopen("FILE.TXT", "r")));
    assert(!fopen("FILE.TXT", "w") && errno == EMFILE);
    for (unsigned i = 0; i < 29; ++i) assert(!fclose(all[i]));
    assert(remove("FILE.TXT") == -1 && errno == RUM_EBUSY && rename("a", "b") == -1 && errno == EACCES);
    f = fopen("FILE.TXT", "w"); assert(f && !fputs("one\ntwo", f) && !fclose(f));
    f = fopen("FILE.TXT", "r"); assert(f && fgets(out, sizeof out, f) && !strcmp(out, "one\n"));
    assert(fgets(out, sizeof out, f) && !strcmp(out, "two") && !fgets(out, sizeof out, f) && feof(f)); assert(!fclose(f));
    f = fopen("FILE.TXT", "r"); close_error = 1; assert(fclose(f) == EOF && errno == EIO); close_error = 0;
    f = fopen("FILE.TXT", "w"); assert(f); write_fail_at = 5;
    assert(fwrite("abcdefghijkl", 3, 4, f) == 1 && disk_bytes == 5 && ferror(f) && errno == ENOSPC);
    assert(!memcmp(disk, "abcde", 5)); write_fail_at = SIZE_MAX; assert(!fclose(f));
}
int host_snprintf(char *, size_t, const char *, ...) __asm__("snprintf");
static void formatting_checks(void)
{
    char text[256];
    assert(snprintf(text, sizeof text, "%s:%+06d:%#x:%llu:%.3s", "rum", -42, 0xabu, UINT64_MAX, "abcdef") == 40);
    assert(!strcmp(text, "rum:-00042:0xab:18446744073709551615:abc"));
    assert(snprintf(text, 4, "abcdef") == 6 && !strcmp(text, "abc"));
    assert(snprintf(NULL, 0, "%01000u", 3u) == 1000);
    assert(snprintf(text, sizeof text, "%lld", INT64_MIN) == 20 && !strcmp(text, "-9223372036854775808"));
    assert(snprintf(text, sizeof text, "%#o|%.0u|%*.*s|%zu", 0u, 0u, -5, 2, "abcd", (size_t)17) == 11);
    assert(!strcmp(text, "0||ab   |17"));
    assert(snprintf(text, sizeof text, "%2147483648d", 1) == -1 && errno == EOVERFLOW);
    assert(snprintf(text, sizeof text, "%f", 1.0) == -1 && errno == EINVAL);
    assert(snprintf(text, sizeof text, "%n", &errno) == -1 && errno == EINVAL);
    FILE *file = fopen("FILE.TXT", "w"); assert(file);
    assert(fprintf(file, "%0300u", 7u) == 300 && disk_bytes == 300 && disk[299] == '7'); assert(!fclose(file));
    char expected[256];
    for (int width = -12; width <= 12; ++width) for (int precision = -1; precision <= 20; ++precision) {
        int actual_count = snprintf(text, sizeof text, "%+0*.*lld|%#.*llX|%#.*o|%-9.4s", width, precision,
            (long long)INT64_MIN, precision, (unsigned long long)UINT64_MAX, precision, 0u, "rum OS");
        int expected_count = host_snprintf(expected, sizeof expected, "%+0*.*lld|%#.*llX|%#.*o|%-9.4s", width, precision,
            (long long)INT64_MIN, precision, (unsigned long long)UINT64_MAX, precision, 0u, "rum OS");
        assert(actual_count == expected_count && !strcmp(text, expected));
    }
}
int main(void)
{
    heap_checks(); string_checks(); io_checks(); formatting_checks();
    const char message[] = "PASS: userspace heap, failed growth, reuse, C strings, file errors and formatting\n";
    assert(write(1, message, sizeof message - 1) == sizeof message - 1); return 0;
}
