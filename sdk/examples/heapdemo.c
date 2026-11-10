#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <rum/user.h>

int main(int argc, char **argv)
{
    uint64_t started, finished;
    if (rum_monotonic_ms(&started) < 0) return 1;
    unsigned char *data = calloc(256 * 1024, 1);
    if (!data) { fputs("heapdemo: allocation failed.\n", stderr); return 1; }
    for (unsigned i = 0; i < 256 * 1024; ++i) {
        if (data[i]) { free(data); return 2; }
        data[i] = (unsigned char)i;
    }
    unsigned char *grown = realloc(data, 768 * 1024);
    if (!grown) { free(data); fputs("heapdemo: growth failed.\n", stderr); return 1; }
    data = grown;
    for (unsigned i = 0; i < 256 * 1024; ++i) if (data[i] != (unsigned char)i) { free(data); return 2; }
    free(data);
    if (argc > 1) {
        FILE *file = fopen(argv[1], "rb");
        if (!file) { fputs("heapdemo: cannot open file.\n", stderr); return 1; }
        unsigned char buffer[512]; uint64_t total = 0; size_t count;
        while ((count = fread(buffer, 1, sizeof buffer, file)) != 0) total += count;
        int failed = ferror(file); if (fclose(file)) failed = 1;
        if (failed) { fputs("heapdemo: read failed.\n", stderr); return 1; }
        if (printf("Read %llu bytes.\n", (unsigned long long)total) < 0) return 1;
    }
    if (rum_monotonic_ms(&finished) < 0) return 1;
    return printf("Heap growth, data preservation and release passed (%llu ms).\n",
        (unsigned long long)(finished - started)) < 0;
}
