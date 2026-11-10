/* Host build check using the same executable policy as the kernel loader. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <rum/abi/elf.h>
#include <rum/abi/layout.h>
#include <rum/elf-image.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The ELF host checker currently requires a little-endian host"
#endif

struct image {
    FILE *file;
    size_t bytes;
    struct rum_elf_header header;
    struct rum_elf_program programs[RUM_ELF_PROGRAM_LIMIT];
};

static bool failure(const char *path, const char *reason)
{
    fprintf(stderr, "rum: %s: %s\n", path, reason);
    return false;
}

static bool read_image(const char *path, struct image *image, bool asset)
{
    FILE *file = image->file = fopen(path, "rb");
    if (!file) return failure(path, "cannot open ELF");
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length >= (long)sizeof(image->header) && length <= 64 * 1024 * 1024 &&
         (!asset || length <= RUM_ELF_IMAGE_LIMIT) && fseek(file, 0, SEEK_SET) == 0;
    if (!ok) return failure(path, "file size outside the supported executable/debug limits");
    image->bytes = (size_t)length;
    if (fread(&image->header, 1, sizeof image->header, file) != sizeof image->header)
        return failure(path, "cannot read ELF header");
    struct rum_elf_header *header = &image->header;
    uint64_t limit = asset ? RUM_ELF_IMAGE_LIMIT : 64 * 1024 * 1024;
    if (!rum_elf_header_valid(header, image->bytes, limit))
        return failure(path, "expected static System V little-endian i386 ELF32 ET_EXEC");
    if (fseek(file, (long)header->phoff, SEEK_SET) ||
        fread(image->programs, sizeof image->programs[0], header->phnum, file) != header->phnum)
        return failure(path, "cannot read program headers");
    struct rum_elf_plan plan;
    return rum_elf_make_plan(header, image->programs, image->bytes, limit, &plan) ||
           failure(path, "unsupported segment, alignment, entry, stack, overlap or memory budget");
}

static bool same_load_image(const struct image *asset, const struct image *debug)
{
    if (asset->header.entry != debug->header.entry) return false;
    unsigned i = 0, j = 0;
    for (;;) {
        /* objcopy may drop empty LOAD/NULL entries without changing any page. */
        while (i < asset->header.phnum && (asset->programs[i].type != RUM_ELF_PT_LOAD ||
               !asset->programs[i].memory_bytes)) ++i;
        while (j < debug->header.phnum && (debug->programs[j].type != RUM_ELF_PT_LOAD ||
               !debug->programs[j].memory_bytes)) ++j;
        if (i == asset->header.phnum || j == debug->header.phnum)
            return i == asset->header.phnum && j == debug->header.phnum;
        const struct rum_elf_program *a = &asset->programs[i++], *b = &debug->programs[j++];
        if (a->address != b->address || a->file_bytes != b->file_bytes ||
            a->memory_bytes != b->memory_bytes || a->flags != b->flags || a->alignment != b->alignment)
            return false;
        unsigned char left[4096], right[4096];
        if (fseek(asset->file, (long)a->offset, SEEK_SET) || fseek(debug->file, (long)b->offset, SEEK_SET)) return false;
        uint32_t remaining = a->file_bytes;
        while (remaining) {
            size_t chunk = remaining < sizeof left ? remaining : sizeof left;
            if (fread(left, 1, chunk, asset->file) != chunk || fread(right, 1, chunk, debug->file) != chunk ||
                memcmp(left, right, chunk)) return false;
            remaining -= (uint32_t)chunk;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: check-user-elf ASSET.elf [DEBUG.elf]\n");
        return 2;
    }
    struct image asset = {0}, debug = {0};
    bool ok = read_image(argv[1], &asset, true);
    if (ok && argc == 3) {
        ok = read_image(argv[2], &debug, false);
        if (ok && !same_load_image(&asset, &debug)) ok = failure(argv[1], "stripping changed the load image");
    }
    if (ok) printf("rum: validated user ELF32: %s (%zu asset bytes)\n", argv[1], asset.bytes);
    if (asset.file) fclose(asset.file);
    if (debug.file) fclose(debug.file);
    return ok ? 0 : 1;
}
