#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <rum/user.h>

#define REPORT 0xffffffd0u
#define PEERS 0xffffffd1u
static void check(bool ok, unsigned code) { if (!ok) rum_exit((int32_t)code); }
static rum_result_t regions(void) { return rum_syscall3(REPORT, 0, 0, 0); }
static void basic(void)
{
    check(regions() == 0 && !malloc(0) && !calloc(0, SIZE_MAX), 1);
    check(!calloc(SIZE_MAX, 2) && errno == ENOMEM && !malloc(SIZE_MAX), 2);
    unsigned char *slots[96];
    for (unsigned i = 0; i < 96; ++i) {
        slots[i] = malloc(i * 97 + 1); check(slots[i] && !((uintptr_t)slots[i] & 15), 3);
        memset(slots[i], (int)i, i * 97 + 1);
    }
    for (unsigned i = 0; i < 96; i += 2) free(slots[i]);
    for (unsigned i = 1; i < 96; i += 2) {
        unsigned char *grown = realloc(slots[i], i * 197 + 8192); check(grown != NULL, 4);
        for (unsigned j = 0; j < i * 97 + 1; ++j) check(grown[j] == i, 5);
        slots[i] = realloc(grown, 17); check(slots[i] && slots[i][16] == i, 6);
    }
    for (unsigned i = 1; i < 96; i += 2) free(slots[i]);
    check(regions() == 0, 7);
    unsigned char *zero = calloc(10001, 1); check(zero != NULL, 8);
    for (unsigned i = 0; i < 10001; ++i) check(!zero[i], 9);
    check(realloc(zero, 0) == NULL && regions() == 0, 10);
    zero = realloc(NULL, 65); check(zero != NULL, 11); free(zero);
    check(regions() == 0, 12);
}
static void files(void)
{
    unsigned char data[93], copy[94];
    for (unsigned i = 0; i < sizeof data; ++i) data[i] = (unsigned char)(i * 7);
    FILE *file = fopen("/DATA.TXT", "w+b"); check(file != NULL, 20);
    check(fwrite(data, 3, 31, file) == 31 && ftell(file) == sizeof data && fflush(file) == 0, 21);
    check(fseek(file, 0, SEEK_SET) == 0 && fread(copy, 1, sizeof copy, file) == sizeof data &&
          feof(file) && !ferror(file) && !memcmp(data, copy, sizeof data), 22);
    rewind(file); check(!feof(file) && ftell(file) == 0 && fgetc(file) == 0, 23);
    check(remove("/DATA.TXT") == -1 && errno == RUM_EBUSY && fclose(file) == 0, 24);
    file = fopen("/DATA.TXT", "ab"); check(file && fwrite("END", 1, 3, file) == 3 && !fclose(file), 25);
    file = fopen("/DATA.TXT", "rb"); check(file && !fseek(file, -3, SEEK_END) &&
        fread(copy, 1, 3, file) == 3 && !memcmp(copy, "END", 3) && !fclose(file), 26);
    file = fopen("/DATA.TXT", "w"); check(file && fprintf(file, "%s %08X %llu\n", "rum", 0x123u, UINT64_MAX) > 0 && !fclose(file), 27);
    file = fopen("/DATA.TXT", "r"); check(file && fgets((char *)copy, sizeof copy, file) &&
        !strcmp((char *)copy, "rum 00000123 18446744073709551615\n") && !fclose(file), 28);
    check(!rename("/DATA.TXT", "/MOVED.TXT") && !remove("/MOVED.TXT"), 29);
    check(!fopen("/MISSING.TXT", "r") && errno == ENOENT && !fopen("/DATA.TXT", "wbb") && errno == EINVAL, 30);
}
static void clocks(void)
{
    uint64_t before, after;
    check(!rum_monotonic_ms(&before) && !rum_sleep(21) && !rum_monotonic_ms(&after) && after >= before + 21, 40);
    check(rum_monotonic_ms(NULL) == -RUM_EINVAL && rum_clock(NULL) == -RUM_EFAULT, 41);
    struct rum_clock_request bad = { .version = RUM_CLOCK_ABI_VERSION, .size = sizeof bad, .reserved = {1} };
    check(rum_clock(&bad) == -RUM_EINVAL && bad.reserved[0] == 1 && !bad.tick_hz, 42);
    bad.reserved[0] = 0; bad.size--;
    check(rum_clock(&bad) == -RUM_EINVAL, 43);
    bad.size++; bad.version++;
    check(rum_clock(&bad) == -RUM_EINVAL, 44);
    --bad.version;
    check(rum_syscall3(RUM_SYS_CLOCK, (uint32_t)(uintptr_t)&bad, 1, 0) == -RUM_EINVAL, 45);
    struct rum_vm_allocate_request memory = { .version = RUM_VM_ABI_VERSION, .size = sizeof memory,
        .bytes = 8192, .protection = RUM_VM_READ | RUM_VM_WRITE };
    check(!rum_vm_allocate(&memory), 46);
    struct rum_clock_request *cross = (void *)(uintptr_t)(memory.address + 4096 - 16);
    *cross = (struct rum_clock_request){ .version = RUM_CLOCK_ABI_VERSION, .size = sizeof *cross };
    check(!rum_clock(cross) && cross->tick_hz && !rum_vm_protect(memory.address, memory.bytes, RUM_VM_READ), 47);
    check(rum_clock(cross) == -RUM_EFAULT && !rum_vm_release(memory.address, memory.bytes), 48);
}
static void exhaust(void)
{
    unsigned char *slots[32]; unsigned count = 0;
    for (; count < 32; ++count) {
        slots[count] = malloc(1024 * 1024);
        if (!slots[count]) break;
        slots[count][0] = (unsigned char)count; slots[count][1024 * 1024 - 1] = (unsigned char)(count ^ 0x7f);
    }
    check(count > 0 && count < 32 && errno == ENOMEM, 50);
    check(!realloc(slots[0], 16 * 1024 * 1024), 51);
    for (unsigned i = 0; i < count; ++i) {
        check(slots[i][0] == i && slots[i][1024 * 1024 - 1] == (i ^ 0x7f), 52); free(slots[i]);
    }
    check(!regions(), 53); void *again = calloc(8193, 1); check(again != NULL, 54); free(again);
}
int main(int argc, char **argv)
{
    check(argc == 3, 60);
    char mode = argv[1][0]; unsigned index = (unsigned)(argv[2][0] - '0');
    if (mode == 'i') { basic(); files(); clocks(); return 0; }
    if (mode == 'e') { exhaust(); return 0; }
    if (mode == 'o') { check(!malloc(1) && errno == ENOMEM && !regions(), 61); return 0; }
    unsigned char *data = malloc(1024 * 1024); check(data != NULL, 62);
    memset(data, (int)(index + 1), 1024 * 1024);
    FILE *leak = NULL;
    if (mode != 'n') { leak = fopen("/LEAK.TXT", "r"); check(leak != NULL, 63); }
    check(rum_syscall3(REPORT, (uint32_t)(uintptr_t)data, 1024 * 1024, index + 1) == 0, 64);
    if (mode == 'f') __asm__ volatile("ud2");
    if (mode == 's') for (;;) __asm__ volatile("pause");
    if (mode == 'n') {
        while (rum_syscall3(PEERS, 0, 0, 0) != 2) __asm__ volatile("pause");
        for (unsigned i = 0; i < 500000; ++i) {
            unsigned offset = (i * 313u) & (1024 * 1024 - 1);
            check(*(volatile unsigned char *)(data + offset) == index + 1, 65);
        }
        free(data); check(!regions(), 66);
    }
    return 0; /* l intentionally leaves its heap and FILE open. */
}
