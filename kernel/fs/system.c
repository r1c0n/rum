#include <rum/memory.h>
#include <rum/path.h>
#include <rum/pmm.h>
#include <rum/system.h>

static uint32_t system_image_address, system_image_pages;
static const struct system_header *image;
static bool mounted;

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static const struct system_entry *entries(const struct system_header *header)
{
    return (const void *)((const char *)header + sizeof *header);
}

static bool valid_image(const struct system_header *header, uint32_t bytes)
{
    if (bytes < sizeof *header || bytes > SYSTEM_IMAGE_LIMIT ||
        memcmp(header->magic, "RUMSYS1", 8) || header->version != 1 || header->bytes != bytes ||
        !header->files || header->files > SYSTEM_FILE_LIMIT || header->entry_bytes != sizeof(struct system_entry) ||
        header->header_bytes != sizeof *header || header->reserved) return false;
    uint32_t offset = sizeof *header + header->files * sizeof(struct system_entry);
    if (offset > bytes) return false;
    const struct system_entry *files = entries(header);
    for (unsigned i = 0; i < header->files; ++i) {
        const struct system_entry *file = &files[i];
        unsigned end = 0;
        while (end < sizeof file->name && file->name[end]) ++end;
        if (end == sizeof file->name || fs_name_check(FS_NAMES_RAM, file->name) != FS_OK ||
            file->reserved[0] || file->reserved[1] || file->offset != offset ||
            file->bytes > SYSTEM_FILE_BYTES || file->bytes > bytes - offset) return false;
        for (; end < sizeof file->name; ++end) if (file->name[end]) return false;
        for (unsigned j = 0; j < i; ++j) if (equal(file->name, files[j].name)) return false;
        offset += file->bytes;
        uint32_t aligned = (offset + 3u) & ~3u;
        if (aligned > bytes) return false;
        while (offset < aligned) if (((const unsigned char *)header)[offset++]) return false;
    }
    return offset == bytes;
}

enum fs_error system_files_prepare(const struct multiboot_info *info)
{
    if (image) return FS_BUSY;
    if (!info || !(info->flags & (1u << 3)) || !info->mods_count || !info->mods_addr)
        return FS_UNAVAILABLE;
    const struct multiboot_module *module = (const void *)(uintptr_t)info->mods_addr;
    /* Multiboot requires the OS to ignore the module's reserved word. Only
       our archive's own versioned reserved fields need to be zero. */
    if (!module->start || module->end <= module->start ||
        !valid_image((const void *)(uintptr_t)module->start, module->end - module->start)) return FS_INVALID;
    uint32_t bytes = module->end - module->start;
    uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t physical = pmm_allocate_contiguous(pages);
    if (!physical) return FS_NO_MEMORY;
    memset((void *)(uintptr_t)physical, 0, pages * PAGE_SIZE);
    memcpy((void *)(uintptr_t)physical, (const void *)(uintptr_t)module->start, bytes);
    system_image_address = physical; system_image_pages = pages;
    image = (const void *)(uintptr_t)physical;
    return FS_OK;
}

static enum fs_error stat(void *unused, uint64_t id, struct fs_node *node)
{
    (void)unused;
    if (!image || !id || id > (uint64_t)image->files + 1) return FS_NOT_FOUND;
    *node = (struct fs_node){ .id = id, .kind = id == 1 ? FS_DIRECTORY : FS_FILE,
                             .size = id == 1 ? 0 : entries(image)[id - 2].bytes };
    return FS_OK;
}
static enum fs_error lookup(void *unused, uint64_t parent, const char *name, struct fs_node *node)
{
    if (parent != 1) return FS_NOT_DIRECTORY;
    for (unsigned i = 0; image && i < image->files; ++i)
        if (equal(name, entries(image)[i].name)) return stat(unused, i + 2, node);
    return FS_NOT_FOUND;
}
static enum fs_error retain(void *unused, uint64_t id)
{
    struct fs_node node;
    return stat(unused, id, &node);
}
static void release(void *unused, uint64_t id) { (void)unused; (void)id; }
static struct fs_io_result read(void *unused, uint64_t id, uint64_t offset, void *buffer, size_t bytes)
{
    struct fs_node node;
    enum fs_error error = stat(unused, id, &node);
    if (error != FS_OK || node.kind != FS_FILE)
        return (struct fs_io_result){ .error = error != FS_OK ? error : FS_IS_DIRECTORY };
    if (offset >= node.size) return (struct fs_io_result){0};
    if (bytes > node.size - offset) bytes = (size_t)(node.size - offset);
    memcpy(buffer, (const char *)image + entries(image)[id - 2].offset + (size_t)offset, bytes);
    return (struct fs_io_result){ .transferred = bytes };
}
static enum fs_error readdir(void *unused, uint64_t id, uint64_t cursor,
                             struct fs_entry *entry, uint64_t *next)
{
    (void)unused;
    if (id != 1) return FS_NOT_DIRECTORY;
    if (!image || cursor >= image->files) return FS_END;
    const struct system_entry *file = &entries(image)[cursor];
    *entry = (struct fs_entry){ .kind = FS_FILE, .size = file->bytes };
    memcpy(entry->name, file->name, sizeof entry->name);
    *next = cursor + 1;
    return FS_OK;
}
static const struct fs_operations operations = {
    .lookup = lookup, .stat = stat, .retain = retain, .release = release, .read = read, .readdir = readdir,
};

enum fs_error system_files_mount(void)
{
    if (mounted) return FS_BUSY;
    if (!image) return FS_UNAVAILABLE;
    const struct fs_backend backend = { .operations = &operations, .root = 1,
                                       .naming = FS_NAMES_RAM, .read_only = true };
    enum fs_error error = fs_mount_system(&backend);
    if (error == FS_OK) mounted = true;
    else {
        (void)pmm_free_contiguous(system_image_address, system_image_pages);
        image = NULL; system_image_address = system_image_pages = 0;
    }
    return error;
}
