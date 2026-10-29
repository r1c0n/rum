/* Host build check, not the kernel's future untrusted executable loader. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rum/abi/elf.h>
#include <rum/abi/layout.h>
#include <rum/process_limits.h>
#include <rum/ramfs.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The ELF host checker currently requires a little-endian host"
#endif

struct image {
    unsigned char *data;
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
    FILE *file = fopen(path, "rb");
    if (!file) return failure(path, "cannot open ELF");
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length >= (long)sizeof(image->header) && length <= 64 * 1024 * 1024 &&
         (!asset || length <= RAMFS_FILE_LIMIT) && fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        image->bytes = (size_t)length;
        image->data = malloc(image->bytes);
        ok = image->data && fread(image->data, 1, image->bytes, file) == image->bytes;
    }
    fclose(file);
    if (!ok) return failure(path, "invalid file size, read failure or 64 KiB asset limit");
    memcpy(&image->header, image->data, sizeof(image->header));
    struct rum_elf_header *header = &image->header;
    if (memcmp(header->ident, "\177ELF\1\1\1\0\0", 9) ||
        header->type != RUM_ELF_EXEC || header->machine != RUM_ELF_I386 ||
        header->version != RUM_ELF_VERSION || header->flags ||
        header->ehsize != sizeof(*header) || header->phentsize != sizeof(image->programs[0]) ||
        !header->phnum || header->phnum > RUM_ELF_PROGRAM_LIMIT ||
        header->phoff < sizeof(*header) || header->phoff > image->bytes ||
        (size_t)header->phnum * header->phentsize > image->bytes - header->phoff)
        return failure(path, "expected static System V little-endian i386 ELF32 ET_EXEC");
    memcpy(image->programs, image->data + header->phoff, header->phnum * header->phentsize);
    bool entry = false, stack = false;
    uint32_t previous_end = RUM_ABI_PROGRAM_BASE;
    uint32_t pages = RUM_ABI_STACK_SIZE / RUM_ABI_PAGE_SIZE;
    for (unsigned i = 0; i < header->phnum; ++i) {
        const struct rum_elf_program *program = &image->programs[i];
        if (program->type == RUM_ELF_PT_NULL) continue;
        if (program->type == RUM_ELF_PT_GNU_STACK) {
            if (stack || program->flags != (RUM_ELF_PF_R | RUM_ELF_PF_W) ||
                program->file_bytes || program->memory_bytes)
                return failure(path, "expected one nonexecutable empty stack descriptor");
            stack = true;
            continue;
        }
        if (program->type != RUM_ELF_PT_LOAD)
            return failure(path, "unsupported interpreter, dynamic, TLS or program header");
        if (program->file_bytes > program->memory_bytes || program->offset > image->bytes ||
            program->file_bytes > image->bytes - program->offset ||
            !(program->flags & RUM_ELF_PF_R) || (program->flags & ~7u) ||
            ((program->flags & (RUM_ELF_PF_W | RUM_ELF_PF_X)) == (RUM_ELF_PF_W | RUM_ELF_PF_X)))
            return failure(path, "invalid segment size, bounds or permissions");
        if (!program->memory_bytes) continue;
        uint32_t align = program->alignment;
        if ((align > 1 && ((align & (align - 1)) ||
             (program->address % align) != (program->offset % align))) ||
            (program->address % RUM_ABI_PAGE_SIZE) != (program->offset % RUM_ABI_PAGE_SIZE))
            return failure(path, "invalid segment alignment");
        if (program->address < RUM_ABI_PROGRAM_BASE || program->address >= RUM_ABI_PROGRAM_END ||
            program->memory_bytes > RUM_ABI_PROGRAM_END - program->address)
            return failure(path, "segment outside user program window");
        uint32_t start = program->address & ~(RUM_ABI_PAGE_SIZE - 1u);
        uint32_t end = (program->address + program->memory_bytes + RUM_ABI_PAGE_SIZE - 1u) &
                       ~(RUM_ABI_PAGE_SIZE - 1u);
        if (start < previous_end) return failure(path, "unordered or page-overlapping segments");
        previous_end = end;
        uint32_t count = (end - start) / RUM_ABI_PAGE_SIZE;
        if (count > RUM_PROCESS_USER_BYTES / RUM_ABI_PAGE_SIZE - pages)
            return failure(path, "mapped image plus user stack exceeds process budget");
        pages += count;
        if ((program->flags & RUM_ELF_PF_X) && header->entry >= program->address &&
            header->entry - program->address < program->file_bytes) entry = true;
    }
    if (!entry || !stack) return failure(path, "missing file-backed executable entry or stack descriptor");
    return true;
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
        if (memcmp(asset->data + a->offset, debug->data + b->offset, a->file_bytes)) return false;
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
    free(asset.data);
    free(debug.data);
    return ok ? 0 : 1;
}
