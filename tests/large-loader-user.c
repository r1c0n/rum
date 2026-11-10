#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <rum/user.h>

#define INJECT 0xffffffc0u
#define READY 0xffffffc1u
extern const unsigned char large_rodata[131079];
static volatile unsigned char initialized[65539] = { [0 ... 65538] = 0xa7 };
static volatile unsigned char zeroed[524305];
static void check(bool ok, unsigned code) { if (!ok) rum_exit((int32_t)code); }
static void parent(void)
{
    rum_result_t fd = rum_open("/CANARY.TXT", RUM_OPEN_READ, 0); check(fd >= 3, 30);
    char text[3]; check(rum_read((rum_handle_t)fd, text, 2) == 2 && !memcmp(text, "pa", 2), 31);
    rum_result_t executable = rum_open("/LARGE.ELF", RUM_OPEN_READ, 0); check(executable >= 3, 46);
    char ident[7]; check(rum_read((rum_handle_t)executable, ident, sizeof ident) == sizeof ident &&
        !memcmp(ident, "\177ELF\1\1\1", sizeof ident), 47);
    unsigned char *owned = malloc(8193); check(owned != NULL, 32); memset(owned, 0x73, 8193);
    struct rum_file_information before, after;
    check(!rum_fstat((rum_handle_t)fd, &before), 33);
    struct rum_arguments arguments = { .argc = 2, .string_bytes = 12, .offsets = {0, 10}, .strings = "LARGE.ELF\0n" };
    struct rum_handle_mapping mappings[] = { { .source = (rum_handle_t)fd, .target = 3 },
                                            { .source = (rum_handle_t)executable, .target = 4 } };
    const int errors[] = { RUM_EIO, RUM_ETIMEDOUT, RUM_EIO, RUM_EIO, RUM_EIO, RUM_ENOMEM };
    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
        check(!rum_syscall3(INJECT, i + 1, 0, 0), 34);
        check(rum_spawn_with_handles("/LARGE.ELF", &arguments, mappings, 2, 0) == -errors[i], 35 + i);
        check(!rum_syscall3(INJECT, 0, 0, 0), 41);
        uint64_t position;
        check(!rum_seek((rum_handle_t)fd, 0, RUM_SEEK_CUR, &position) && position == 2 &&
              !rum_fstat((rum_handle_t)fd, &after) && !memcmp(&before, &after, sizeof before), 42);
        check(rum_read((rum_handle_t)fd, text, 2) == 2 && !memcmp(text, "re", 2) &&
              !rum_seek((rum_handle_t)fd, 2, RUM_SEEK_SET, &position), 43);
        check(!rum_seek((rum_handle_t)executable, 0, RUM_SEEK_CUR, &position) && position == sizeof ident, 48);
        for (unsigned n = 0; n < 8193; ++n) check(owned[n] == 0x73, 44);
    }
    free(owned); check(!rum_close((rum_handle_t)fd) && !rum_close((rum_handle_t)executable), 45);
}
int main(int argc, char **argv)
{
    check(argc == 2, 1);
    char mode = argv[1][0];
    if (mode == 'p') { parent(); return 0; }
    for (unsigned i = 0; i < sizeof large_rodata; ++i) check(large_rodata[i] == (unsigned char)(i * 17 + 3), 2);
    for (unsigned i = 0; i < sizeof initialized; ++i) { check(initialized[i] == 0xa7, 3); initialized[i] = (unsigned char)i; }
    for (unsigned i = 0; i < sizeof zeroed; ++i) { check(!zeroed[i], 4); zeroed[i] = (unsigned char)(i * 7); }
    unsigned char *heap = calloc(524289, 1); check(heap != NULL, 5);
    for (unsigned i = 0; i < 524289; ++i) check(!heap[i], 6);
    if (mode == 'f') *(volatile unsigned char *)(uintptr_t)large_rodata = 0;
    if (mode == 'g') *(volatile unsigned char *)(uintptr_t)(RUM_ABI_STACK_BASE - 1) = 0;
    if (mode == 's') { check(!rum_syscall3(READY, 0, 0, 0), 7); for (;;) __asm__ volatile("pause"); }
    if (mode != 'l') free(heap);
    return 0;
}
