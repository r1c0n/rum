#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <rum/user.h>

int main(int argc, char **argv)
{
    unsigned char *memory = calloc(8193, 1);
    if (!memory) return 1;
    for (unsigned i = 0; i < 8193; ++i) { if (memory[i]) return 2; memory[i] = (unsigned char)(i * 17); }
    unsigned char *grown = realloc(memory, 262145);
    if (!grown) { free(memory); return 1; }
    memory = grown;
    for (unsigned i = 0; i < 8193; ++i) if (memory[i] != (unsigned char)(i * 17)) return 2;
    if (calloc(SIZE_MAX, 2) != NULL || errno != ENOMEM) return 3;
    if (argc > 1 && !strcmp(argv[1], "--fault")) __asm__ volatile("ud2");
    if (argc > 1 && !strcmp(argv[1], "--spin")) {
        puts("sdk cancellation ready"); for (;;) __asm__ volatile("pause");
    }
    if (argc > 1) {
        FILE *file = fopen(argv[1], "w+b");
        if (!file) { free(memory); return 4; }
        unsigned char copy[257];
        if (fwrite(memory, 1, 8193, file) != 8193 || fflush(file) || fseek(file, 0, SEEK_SET)) return 5;
        unsigned offset = 0; size_t count;
        while ((count = fread(copy, 1, sizeof copy, file)) != 0) {
            if (offset > 8193 || count > 8193 - offset || memcmp(copy, memory + offset, count)) return 6;
            offset += count;
        }
        int failed = ferror(file) || offset != 8193;
        if (fclose(file) || failed) return 7;
    }
    uint64_t before, after;
    if (rum_monotonic_ms(&before) || rum_sleep(1) || rum_monotonic_ms(&after) || after < before) return 8;
    free(memory);
    return puts("External SDK application passed.") == EOF;
}
