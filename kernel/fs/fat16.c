#include <rum/cpu.h>
#include <rum/fat16.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/path.h>

#define ROOT_ID UINT64_C(1)
#define ENTRY_BYTES 32u
#define ENTRIES_PER_SECTOR (FAT16_SECTOR_SIZE / ENTRY_BYTES)
#define FIRST_RESERVED 0xfff0u
#define FIRST_END 0xfff8u
#define MAX_CLUSTERS 65524u
struct pin { uint64_t id; uint32_t count; };
struct description { struct fs_node node; uint32_t first, length; };
static struct {
    struct block_device *device;
    struct fat16_information info;
    unsigned char *fat;
    unsigned char sector[FAT16_SECTOR_SIZE];
    unsigned char visited[(MAX_CLUSTERS + 2u + 7u) / 8u];
    struct pin pins[FS_OBJECT_LIMIT];
    struct fs_io_result last_io;
    uint32_t cached_sector;
    bool ready, busy, cache_valid;
} volume;

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t u32(const unsigned char *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static enum fs_error read_sectors(uint32_t sector, uint32_t count, void *data)
{
    if (sector >= volume.info.sectors || count > volume.info.sectors - sector) return FS_RANGE;
    struct block_result result = block_read(volume.device, sector, count, data, (size_t)count * FAT16_SECTOR_SIZE);
    volume.last_io = (struct fs_io_result){ .error = fs_error_from_block(result.error),
        .device_status = result.status, .device_error = result.device_error };
    if (result.error == BLOCK_OK && result.completed != count) volume.last_io.error = FS_IO_ERROR;
    return volume.last_io.error;
}
static enum fs_error sector(uint32_t number)
{
    if (volume.cache_valid && number == volume.cached_sector) return FS_OK;
    volume.cache_valid = false;
    enum fs_error error = read_sectors(number, 1, volume.sector);
    if (error == FS_OK) { volume.cached_sector = number; volume.cache_valid = true; }
    return error;
}
static bool valid_cluster(uint32_t cluster)
{
    return cluster >= 2 && cluster < FIRST_RESERVED && cluster <= volume.info.clusters + 1;
}
static uint16_t next_cluster(uint32_t cluster) { return u16(volume.fat + cluster * 2); }

/* Validate the whole chain, even when a caller only requests its first byte.
   A fixed bitmap detects repeats without allocating or trusting chain length. */
static enum fs_error chain(uint32_t first, uint32_t *length)
{
    memset(volume.visited, 0, sizeof volume.visited);
    uint32_t cluster = first;
    for (uint32_t count = 1; count <= volume.info.clusters; ++count) {
        if (!valid_cluster(cluster)) return FS_IO_ERROR;
        unsigned char mask = (unsigned char)(1u << (cluster % 8));
        if (volume.visited[cluster / 8] & mask) return FS_IO_ERROR;
        volume.visited[cluster / 8] |= mask;
        uint32_t next = next_cluster(cluster);
        if (next >= FIRST_END) { *length = count; return FS_OK; }
        if (!valid_cluster(next)) return FS_IO_ERROR;
        cluster = next;
    }
    return FS_IO_ERROR;
}

/* A physical directory slot supplies the stable, mount-local object ID.
   Only root slots and complete data clusters may hold directory entries. */
static bool slot_offset(uint64_t id, uint64_t *offset)
{
    if (id < 2 || id - 2 > UINT64_MAX / ENTRY_BYTES) return false;
    uint64_t bytes = (id - 2) * ENTRY_BYTES;
    uint64_t root = (uint64_t)volume.info.root_start * FAT16_SECTOR_SIZE;
    uint64_t data = (uint64_t)volume.info.data_start * FAT16_SECTOR_SIZE;
    uint64_t end = data + (uint64_t)volume.info.clusters * volume.info.cluster_bytes;
    if (!((bytes >= root && bytes - root < (uint64_t)volume.info.root_entries * ENTRY_BYTES) ||
          (bytes >= data && bytes < end))) return false;
    *offset = bytes; return true;
}
static enum fs_error short_name(const unsigned char *entry, char *name)
{
    unsigned length = 0;
    for (unsigned part = 0; part < 2; ++part) {
        unsigned start = part ? 8 : 0, limit = part ? 3 : 8, count = limit;
        while (count && entry[start + count - 1] == ' ') --count;
        if (!part && !count) return FS_IO_ERROR;
        if (part && count) name[length++] = '.';
        for (unsigned i = 0; i < count; ++i) {
            unsigned char c = entry[start + i];
            if (c == ' ' || c == 0) return FS_IO_ERROR;
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
            name[length++] = (char)c;
        }
    }
    name[length] = 0;
    /* Valid FAT names outside our ASCII subset are deliberately inaccessible. */
    return fs_name_check(FS_NAMES_FAT83, name) == FS_OK ? FS_OK : FS_NOT_FOUND;
}
static enum fs_error decode(const unsigned char *entry, uint64_t id,
                            struct description *description, char *name)
{
    if (!entry[0]) return FS_END;
    if (entry[0] == 0xe5 || (entry[11] & 8)) return FS_NOT_FOUND; /* Deleted, label, LFN. */
    if (entry[0] == '.' && entry[1] == ' ') return FS_NOT_FOUND;
    if (entry[0] == '.' && entry[1] == '.' && entry[2] == ' ') return FS_NOT_FOUND;
    enum fs_error error = short_name(entry, name);
    if (error != FS_OK) return error;
    if ((entry[11] & 0xc0) || u16(entry + 20)) return FS_IO_ERROR;
    *description = (struct description){ .node = { .id = id, .size = u32(entry + 28),
        .kind = (entry[11] & 0x10) ? FS_DIRECTORY : FS_FILE }, .first = u16(entry + 26) };
    if (description->node.kind == FS_DIRECTORY) {
        if (description->node.size) return FS_IO_ERROR;
        error = chain(description->first, &description->length);
        if (error == FS_OK && (uint64_t)description->length * volume.info.cluster_bytes / ENTRY_BYTES >
                              FAT16_DIRECTORY_ENTRY_LIMIT) error = FS_UNSUPPORTED;
        return error;
    }
    if (!description->node.size) return description->first ? FS_IO_ERROR : FS_OK;
    uint64_t required = (description->node.size + volume.info.cluster_bytes - 1) / volume.info.cluster_bytes;
    if (required > volume.info.clusters) return FS_IO_ERROR;
    error = chain(description->first, &description->length);
    return error != FS_OK ? error : description->length == required ? FS_OK : FS_IO_ERROR;
}
static enum fs_error describe(uint64_t id, struct description *description)
{
    if (!volume.ready) return FS_UNAVAILABLE;
    if (id == ROOT_ID) {
        *description = (struct description){ .node = { .id = ROOT_ID, .kind = FS_DIRECTORY } };
        return FS_OK;
    }
    uint64_t offset;
    if (!slot_offset(id, &offset)) return FS_RANGE;
    enum fs_error error = sector((uint32_t)(offset / FAT16_SECTOR_SIZE));
    if (error != FS_OK) return error;
    char name[FS_NAME_CAPACITY];
    error = decode(volume.sector + offset % FAT16_SECTOR_SIZE, id, description, name);
    return error == FS_END ? FS_NOT_FOUND : error;
}
static uint32_t cluster_at(const struct description *description, uint32_t index)
{
    uint32_t cluster = description->first;
    /* description's complete chain was already validated against cached FAT. */
    while (index--) cluster = next_cluster(cluster);
    return cluster;
}
static uint32_t cluster_sector(uint32_t cluster)
{
    return volume.info.data_start + (cluster - 2) * (volume.info.cluster_bytes / FAT16_SECTOR_SIZE);
}
static uint64_t directory_slots(const struct description *description)
{
    return description->node.id == ROOT_ID ? volume.info.root_entries :
           (uint64_t)description->length * volume.info.cluster_bytes / ENTRY_BYTES;
}
static enum fs_error directory_entry(const struct description *directory, uint64_t cursor,
                                     struct description *entry, char *name)
{
    uint64_t count = directory_slots(directory);
    if (cursor >= count) return FS_END;
    uint32_t number, slot;
    if (directory->node.id == ROOT_ID) {
        number = volume.info.root_start + (uint32_t)(cursor / ENTRIES_PER_SECTOR);
        slot = (uint32_t)(cursor % ENTRIES_PER_SECTOR);
    } else {
        uint32_t per_cluster = volume.info.cluster_bytes / ENTRY_BYTES;
        uint32_t cluster = cluster_at(directory, (uint32_t)(cursor / per_cluster));
        uint32_t within = (uint32_t)(cursor % per_cluster);
        number = cluster_sector(cluster) + within / ENTRIES_PER_SECTOR;
        slot = within % ENTRIES_PER_SECTOR;
    }
    enum fs_error error = sector(number);
    if (error != FS_OK) return error;
    uint64_t id = (uint64_t)number * ENTRIES_PER_SECTOR + slot + 2;
    return decode(volume.sector + slot * ENTRY_BYTES, id, entry, name);
}
static enum fs_error lookup(void *context, uint64_t parent, const char *name, struct fs_node *node)
{
    (void)context;
    struct description directory;
    enum fs_error error = describe(parent, &directory);
    if (error != FS_OK) return error;
    if (directory.node.kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    uint64_t count = directory_slots(&directory);
    for (uint64_t cursor = 0; cursor < count; ++cursor) {
        struct description entry; char decoded[FS_NAME_CAPACITY];
        error = directory_entry(&directory, cursor, &entry, decoded);
        if (error == FS_END) return FS_NOT_FOUND;
        if (error == FS_NOT_FOUND) continue;
        if (error != FS_OK) return error;
        if (equal(decoded, name)) { *node = entry.node; return FS_OK; }
    }
    return FS_NOT_FOUND;
}
static enum fs_error stat_node(void *context, uint64_t id, struct fs_node *node)
{
    (void)context; struct description description;
    enum fs_error error = describe(id, &description);
    if (error == FS_OK) *node = description.node;
    return error;
}
static enum fs_error retain(void *context, uint64_t id)
{
    (void)context;
    uint64_t offset;
    if (!volume.ready) return FS_UNAVAILABLE;
    if (id != ROOT_ID && !slot_offset(id, &offset)) return FS_RANGE;
    struct pin *empty = NULL;
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i) {
        struct pin *pin = &volume.pins[i];
        if (!pin->count) { if (!empty) empty = pin; }
        else if (pin->id == id) {
            if (pin->count == UINT32_MAX) return FS_NO_SPACE;
            ++pin->count; return FS_OK;
        }
    }
    if (!empty) return FS_NO_SPACE;
    *empty = (struct pin){ .id = id, .count = 1 }; return FS_OK;
}
static void release(void *context, uint64_t id)
{
    (void)context;
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i)
        if (volume.pins[i].count && volume.pins[i].id == id) { --volume.pins[i].count; return; }
}
static struct fs_io_result read_file(void *context, uint64_t id, uint64_t offset, void *data, size_t bytes)
{
    (void)context;
    struct fs_io_result result = {0};
    volume.last_io = (struct fs_io_result){0};
    struct description description;
    result.error = describe(id, &description);
    if (result.error == FS_OK && description.node.kind != FS_FILE) result.error = FS_IS_DIRECTORY;
    if (result.error == FS_OK && offset < description.node.size) {
        if (bytes > description.node.size - offset) bytes = (size_t)(description.node.size - offset);
        uint32_t index = (uint32_t)(offset / volume.info.cluster_bytes);
        uint32_t cluster = cluster_at(&description, index);
        while (result.transferred < bytes) {
            uint32_t within = (uint32_t)(offset % volume.info.cluster_bytes);
            uint32_t number = cluster_sector(cluster) + within / FAT16_SECTOR_SIZE;
            result.error = sector(number);
            if (result.error != FS_OK) break;
            size_t amount = FAT16_SECTOR_SIZE - within % FAT16_SECTOR_SIZE;
            if (amount > bytes - result.transferred) amount = bytes - result.transferred;
            memcpy((unsigned char *)data + result.transferred, volume.sector + within % FAT16_SECTOR_SIZE, amount);
            result.transferred += amount; offset += amount;
            if (offset % volume.info.cluster_bytes == 0 && result.transferred < bytes) cluster = next_cluster(cluster);
        }
    }
    if (result.error != FS_OK) {
        result.device_status = volume.last_io.device_status; result.device_error = volume.last_io.device_error;
    }
    return result;
}
static enum fs_error readdir_node(void *context, uint64_t id, uint64_t cursor,
                                  struct fs_entry *out, uint64_t *next)
{
    (void)context; struct description directory;
    enum fs_error error = describe(id, &directory);
    if (error != FS_OK) return error;
    if (directory.node.kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    uint64_t count = directory_slots(&directory);
    while (cursor < count) {
        struct description entry; char name[FS_NAME_CAPACITY];
        error = directory_entry(&directory, cursor++, &entry, name);
        if (error == FS_NOT_FOUND) continue;
        if (error != FS_OK) return error;
        *out = (struct fs_entry){ .kind = entry.node.kind, .size = entry.node.size };
        memcpy(out->name, name, sizeof out->name); *next = cursor; return FS_OK;
    }
    return FS_END;
}
static const struct fs_operations operations = {
    .lookup = lookup, .stat = stat_node, .retain = retain, .release = release,
    .read = read_file, .readdir = readdir_node,
};
static const struct fs_backend backend = { .operations = &operations, .root = ROOT_ID,
    .naming = FS_NAMES_FAT83, .read_only = true };

static enum fs_error geometry(struct block_device *device, unsigned char *boot)
{
    if (!device || !device->online) return FS_UNAVAILABLE;
    if (device->sector_size != FAT16_SECTOR_SIZE) return FS_UNSUPPORTED;
    struct block_result read = block_read(device, 0, 1, boot, FAT16_SECTOR_SIZE);
    if (read.error != BLOCK_OK) return fs_error_from_block(read.error);
    if (read.completed != 1) return FS_IO_ERROR;
    uint32_t bytes = u16(boot + 11), per_cluster = boot[13], reserved = u16(boot + 14);
    uint32_t copies = boot[16], entries = u16(boot + 17), small = u16(boot + 19);
    uint32_t large = u32(boot + 32), fat_sectors = u16(boot + 22), total = small ? small : large;
    if (boot[510] != 0x55 || boot[511] != 0xaa || !reserved || !entries ||
        entries % ENTRIES_PER_SECTOR || !fat_sectors || !total || (small && large) ||
        !per_cluster || (per_cluster & (per_cluster - 1)) || u32(boot + 28) ||
        (boot[21] != 0xf0 && boot[21] < 0xf8)) return FS_INVALID;
    if (bytes != FAT16_SECTOR_SIZE || copies != 2 || per_cluster > FAT16_CLUSTER_SECTOR_LIMIT ||
        fat_sectors > FAT16_FAT_SECTOR_LIMIT) return FS_UNSUPPORTED;
    if (total > device->sector_count) return FS_RANGE;
    uint64_t root_start = (uint64_t)reserved + (uint64_t)copies * fat_sectors;
    uint64_t data_start = root_start + (uint64_t)entries / ENTRIES_PER_SECTOR;
    if (data_start >= total) return FS_RANGE;
    uint64_t clusters = (total - data_start) / per_cluster;
    if (clusters < 4085 || clusters > MAX_CLUSTERS) return FS_UNSUPPORTED;
    if ((clusters + 2) * 2 > (uint64_t)fat_sectors * FAT16_SECTOR_SIZE) return FS_RANGE;
    volume.info = (struct fat16_information){ .sectors = total, .clusters = (uint32_t)clusters,
        .cluster_bytes = per_cluster * FAT16_SECTOR_SIZE, .fat_start = reserved, .fat_sectors = fat_sectors,
        .root_start = (uint32_t)root_start, .root_entries = entries, .data_start = (uint32_t)data_start };
    return FS_OK;
}
enum fs_error fat16_mount(struct block_device *device)
{
    if (volume.busy || volume.ready) return FS_BUSY;
    volume.busy = true;
    unsigned char buffer[FAT16_SECTOR_SIZE];
    enum fs_error error = geometry(device, buffer);
    if (error == FS_OK) {
        unsigned media = buffer[21];
        volume.device = device;
        size_t bytes = (size_t)volume.info.fat_sectors * FAT16_SECTOR_SIZE;
        volume.fat = kmalloc(bytes);
        if (!volume.fat) error = FS_NO_MEMORY;
        else error = read_sectors(volume.info.fat_start, volume.info.fat_sectors, volume.fat);
        for (uint32_t i = 0; error == FS_OK && i < volume.info.fat_sectors; ++i) {
            error = read_sectors(volume.info.fat_start + volume.info.fat_sectors + i, 1, buffer);
            if (error == FS_OK && memcmp(volume.fat + (size_t)i * FAT16_SECTOR_SIZE, buffer, FAT16_SECTOR_SIZE))
                error = FS_IO_ERROR;
        }
        if (error == FS_OK && (u16(volume.fat) != (0xff00u | media) ||
            (u16(volume.fat + 2) & 0x3fff) != 0x3fff)) error = FS_IO_ERROR;
        if (error == FS_OK) {
            volume.ready = true;
            error = fs_mount_disk(&backend);
            if (error == FS_OK) volume.info.mounted = true;
        }
    }
    if (error != FS_OK) { (void)kfree(volume.fat); memset(&volume, 0, sizeof volume); }
    volume.busy = false; return error;
}
enum fs_error fat16_unmount(void)
{
    if (volume.busy) return FS_BUSY;
    if (!volume.info.mounted) return FS_UNAVAILABLE;
    volume.busy = true;
    enum fs_error error = fs_unmount_disk();
    if (error == FS_OK) { (void)kfree(volume.fat); memset(&volume, 0, sizeof volume); }
    volume.busy = false; return error;
}
struct fat16_information fat16_info(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct fat16_information info = volume.info;
    cpu_interrupt_restore(flags); return info;
}
