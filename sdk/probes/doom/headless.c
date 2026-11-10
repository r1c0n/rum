/* rum adapter for upstream zone allocation and standard-C WAD file access.
   It does not replace or modify the engine modules being tested. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <rum/user.h>
#include "doomtype.h"
#include "i_system.h"
#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

extern wad_file_class_t stdc_wad_file;
static byte *zone;
byte *I_ZoneBase(int *size)
{
    *size = 1024 * 1024; zone = malloc((size_t)*size);
    if (!zone) { fputs("Doom probe: zone allocation failed.\n", stderr); exit(1); }
    return zone;
}
void I_Error(char *text, ...)
{
    va_list args; va_start(args, text); (void)vfprintf(stderr, text, args); va_end(args); exit(2);
}
long M_FileLength(FILE *file)
{
    long original = ftell(file);
    if (original < 0 || fseek(file, 0, SEEK_END)) return -1;
    long length = ftell(file);
    return fseek(file, original, SEEK_SET) ? -1 : length;
}
static uint32_t little(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int main(int argc, char **argv)
{
    if (argc != 2) { puts("Usage: DOOMIO.ELF path-to-test.wad"); return 1; }
    Z_Init(); Z_CheckHeap();
    wad_file_t *wad = stdc_wad_file.OpenFile(argv[1]);
    if (!wad) { free(zone); return 3; }
    unsigned char header[12], entry[16], payload[13];
    int failed = wad->length != 41 || stdc_wad_file.Read(wad, 0, header, sizeof header) != sizeof header ||
        memcmp(header, "PWAD", 4) || little(header + 4) != 1 || little(header + 8) != 25;
    if (!failed) failed = stdc_wad_file.Read(wad, 25, entry, sizeof entry) != sizeof entry ||
        little(entry) != 12 || little(entry + 4) != sizeof payload || memcmp(entry + 8, "RUMTEST\0", 8);
    if (!failed) failed = stdc_wad_file.Read(wad, 12, payload, sizeof payload) != sizeof payload ||
        memcmp(payload, "rum\0wad\xffprobe", sizeof payload);
    if (!failed) failed = stdc_wad_file.Read(wad, wad->length, payload, 1) != 0;
    stdc_wad_file.CloseFile(wad); Z_CheckHeap(); free(zone);
    uint64_t time;
    if (rum_monotonic_ms(&time)) return 4;
    if (failed) return 5;
    return puts("Doom zone and WAD I/O probe passed.") == EOF;
}
