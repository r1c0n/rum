#include <rum/cpu.h>
#include <rum/fat16.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/path.h>

#include "fat16-internal.h"

struct fat16_volume fat16_volume;

enum fs_error fat16_read_sectors(uint32_t sector, uint32_t count, void *data)
{
    if (sector >= fat16_volume.info.sectors || count > fat16_volume.info.sectors - sector) return FS_RANGE;
    struct block_result result = block_read(fat16_volume.device, sector, count, data, (size_t)count * FAT16_SECTOR_SIZE);
    fat16_volume.last_io = (struct fs_io_result){ .error = fs_error_from_block(result.error),
        .device_status = result.status, .device_error = result.device_error };
    if (result.error == BLOCK_OK && result.completed != count) fat16_volume.last_io.error = FS_IO_ERROR;
    return fat16_volume.last_io.error;
}
enum fs_error fat16_sector(uint32_t number)
{
    if (fat16_volume.cache_valid && number == fat16_volume.cached_sector) return FS_OK;
    fat16_volume.cache_valid = false;
    enum fs_error error = fat16_read_sectors(number, 1, fat16_volume.sector);
    if (error == FS_OK) { fat16_volume.cached_sector = number; fat16_volume.cache_valid = true; }
    return error;
}
bool fat16_valid_cluster(uint32_t cluster)
{
    return cluster >= 2 && cluster < FIRST_RESERVED && cluster <= fat16_volume.info.clusters + 1;
}
uint16_t fat16_next_cluster(uint32_t cluster) { return u16(fat16_volume.fat + cluster * 2); }

/* Validate the whole chain, even when a caller only requests its first byte.
   A fixed bitmap detects repeats without allocating or trusting chain length. */
enum fs_error fat16_chain(uint32_t first, uint32_t *length)
{
    memset(fat16_volume.visited, 0, sizeof fat16_volume.visited);
    uint32_t cluster = first;
    for (uint32_t count = 1; count <= fat16_volume.info.clusters; ++count) {
        if (!fat16_valid_cluster(cluster)) return FS_IO_ERROR;
        unsigned char mask = (unsigned char)(1u << (cluster % 8));
        if (fat16_volume.visited[cluster / 8] & mask) return FS_IO_ERROR;
        fat16_volume.visited[cluster / 8] |= mask;
        uint32_t next = fat16_next_cluster(cluster);
        if (next >= FIRST_END) { *length = count; return FS_OK; }
        if (!fat16_valid_cluster(next)) return FS_IO_ERROR;
        cluster = next;
    }
    return FS_IO_ERROR;
}

/* A physical directory slot supplies the stable, mount-local object ID.
   Only root slots and complete data clusters may hold directory entries. */
bool fat16_slot_offset(uint64_t id, uint64_t *offset)
{
    if (id < 2 || id - 2 > UINT64_MAX / ENTRY_BYTES) return false;
    uint64_t bytes = (id - 2) * ENTRY_BYTES;
    uint64_t root = (uint64_t)fat16_volume.info.root_start * FAT16_SECTOR_SIZE;
    uint64_t data = (uint64_t)fat16_volume.info.data_start * FAT16_SECTOR_SIZE;
    uint64_t end = data + (uint64_t)fat16_volume.info.clusters * fat16_volume.info.cluster_bytes;
    if (!((bytes >= root && bytes - root < (uint64_t)fat16_volume.info.root_entries * ENTRY_BYTES) ||
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
enum fs_error fat16_decode(const unsigned char *entry, uint64_t id,
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
        error = fat16_chain(description->first, &description->length);
        if (error == FS_OK && (uint64_t)description->length * fat16_volume.info.cluster_bytes / ENTRY_BYTES >
                              FAT16_DIRECTORY_ENTRY_LIMIT) error = FS_UNSUPPORTED;
        return error;
    }
    if (!description->node.size) return description->first ? FS_IO_ERROR : FS_OK;
    uint64_t required = (description->node.size + fat16_volume.info.cluster_bytes - 1) / fat16_volume.info.cluster_bytes;
    if (required > fat16_volume.info.clusters) return FS_IO_ERROR;
    error = fat16_chain(description->first, &description->length);
    return error != FS_OK ? error : description->length == required ? FS_OK : FS_IO_ERROR;
}
enum fs_error fat16_describe(uint64_t id, struct description *description)
{
    if (!fat16_volume.ready) return FS_UNAVAILABLE;
    if (fat16_volume.info.faulted) return FS_IO_ERROR;
    if (id == ROOT_ID) {
        *description = (struct description){ .node = { .id = ROOT_ID, .kind = FS_DIRECTORY } };
        return FS_OK;
    }
    uint64_t offset;
    if (!fat16_slot_offset(id, &offset)) return FS_RANGE;
    enum fs_error error = fat16_sector((uint32_t)(offset / FAT16_SECTOR_SIZE));
    if (error != FS_OK) return error;
    char name[FS_NAME_CAPACITY];
    error = fat16_decode(fat16_volume.sector + offset % FAT16_SECTOR_SIZE, id, description, name);
    return error == FS_END ? FS_NOT_FOUND : error;
}
uint32_t fat16_cluster_at(const struct description *description, uint32_t index)
{
    uint32_t cluster = description->first;
    /* description's complete chain was already validated against cached FAT. */
    while (index--) cluster = fat16_next_cluster(cluster);
    return cluster;
}
uint32_t fat16_cluster_sector(uint32_t cluster)
{
    return fat16_volume.info.data_start + (cluster - 2) * (fat16_volume.info.cluster_bytes / FAT16_SECTOR_SIZE);
}
uint64_t fat16_directory_slots(const struct description *description)
{
    return description->node.id == ROOT_ID ? fat16_volume.info.root_entries :
           (uint64_t)description->length * fat16_volume.info.cluster_bytes / ENTRY_BYTES;
}
enum fs_error fat16_directory_slot(const struct description *directory, uint64_t cursor, uint64_t *id)
{
    uint64_t count = fat16_directory_slots(directory);
    if (cursor >= count) return FS_END;
    uint32_t number, slot;
    if (directory->node.id == ROOT_ID) {
        number = fat16_volume.info.root_start + (uint32_t)(cursor / ENTRIES_PER_SECTOR);
        slot = (uint32_t)(cursor % ENTRIES_PER_SECTOR);
    } else {
        uint32_t per_cluster = fat16_volume.info.cluster_bytes / ENTRY_BYTES;
        uint32_t cluster = fat16_cluster_at(directory, (uint32_t)(cursor / per_cluster));
        uint32_t within = (uint32_t)(cursor % per_cluster);
        number = fat16_cluster_sector(cluster) + within / ENTRIES_PER_SECTOR;
        slot = within % ENTRIES_PER_SECTOR;
    }
    *id = (uint64_t)number * ENTRIES_PER_SECTOR + slot + 2;
    return FS_OK;
}
enum fs_error fat16_directory_entry(const struct description *directory, uint64_t cursor,
                                     struct description *entry, char *name)
{
    uint64_t id, offset;
    enum fs_error error = fat16_directory_slot(directory, cursor, &id);
    if (error != FS_OK) return error;
    if (!fat16_slot_offset(id, &offset)) return FS_RANGE;
    error = fat16_sector((uint32_t)(offset / FAT16_SECTOR_SIZE));
    if (error != FS_OK) return error;
    return fat16_decode(fat16_volume.sector + offset % FAT16_SECTOR_SIZE, id, entry, name);
}
static enum fs_error lookup(void *context, uint64_t parent, const char *name, struct fs_node *node)
{
    (void)context;
    struct description directory;
    enum fs_error error = fat16_describe(parent, &directory);
    if (error != FS_OK) return error;
    if (directory.node.kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    uint64_t count = fat16_directory_slots(&directory);
    for (uint64_t cursor = 0; cursor < count; ++cursor) {
        struct description entry; char decoded[FS_NAME_CAPACITY];
        error = fat16_directory_entry(&directory, cursor, &entry, decoded);
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
    enum fs_error error = fat16_describe(id, &description);
    if (error == FS_OK) *node = description.node;
    return error;
}
static enum fs_error retain(void *context, uint64_t id)
{
    (void)context;
    uint64_t offset;
    if (!fat16_volume.ready) return FS_UNAVAILABLE;
    if (id != ROOT_ID && !fat16_slot_offset(id, &offset)) return FS_RANGE;
    struct pin *empty = NULL;
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i) {
        struct pin *pin = &fat16_volume.pins[i];
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
        if (fat16_volume.pins[i].count && fat16_volume.pins[i].id == id) { --fat16_volume.pins[i].count; return; }
}
static struct fs_io_result read_file(void *context, uint64_t id, uint64_t offset, void *data, size_t bytes)
{
    (void)context;
    struct fs_io_result result = {0};
    fat16_volume.last_io = (struct fs_io_result){0};
    struct description description;
    result.error = fat16_describe(id, &description);
    if (result.error == FS_OK && description.node.kind != FS_FILE) result.error = FS_IS_DIRECTORY;
    if (result.error == FS_OK && offset < description.node.size) {
        if (bytes > description.node.size - offset) bytes = (size_t)(description.node.size - offset);
        uint32_t index = (uint32_t)(offset / fat16_volume.info.cluster_bytes);
        uint32_t cluster = fat16_cluster_at(&description, index);
        while (result.transferred < bytes) {
            uint32_t within = (uint32_t)(offset % fat16_volume.info.cluster_bytes);
            uint32_t number = fat16_cluster_sector(cluster) + within / FAT16_SECTOR_SIZE;
            result.error = fat16_sector(number);
            if (result.error != FS_OK) break;
            size_t amount = FAT16_SECTOR_SIZE - within % FAT16_SECTOR_SIZE;
            if (amount > bytes - result.transferred) amount = bytes - result.transferred;
            memcpy((unsigned char *)data + result.transferred, fat16_volume.sector + within % FAT16_SECTOR_SIZE, amount);
            result.transferred += amount; offset += amount;
            if (offset % fat16_volume.info.cluster_bytes == 0 && result.transferred < bytes) cluster = fat16_next_cluster(cluster);
        }
    }
    if (result.error != FS_OK) {
        result.device_status = fat16_volume.last_io.device_status; result.device_error = fat16_volume.last_io.device_error;
    }
    return result;
}
static enum fs_error readdir_node(void *context, uint64_t id, uint64_t cursor,
                                  struct fs_entry *out, uint64_t *next)
{
    (void)context; struct description directory;
    enum fs_error error = fat16_describe(id, &directory);
    if (error != FS_OK) return error;
    if (directory.node.kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    uint64_t count = fat16_directory_slots(&directory);
    while (cursor < count) {
        struct description entry; char name[FS_NAME_CAPACITY] = {0};
        error = fat16_directory_entry(&directory, cursor++, &entry, name);
        if (error == FS_NOT_FOUND) continue;
        if (error != FS_OK) return error;
        *out = (struct fs_entry){ .kind = entry.node.kind, .size = entry.node.size };
        memcpy(out->name, name, sizeof out->name); *next = cursor; return FS_OK;
    }
    return FS_END;
}
static void detach(void *context)
{
    (void)context;
    uint32_t flags = cpu_interrupt_save();
    fat16_volume.info.mounted = false; fat16_volume.ready = false;
    cpu_interrupt_restore(flags);
    bool busy = fat16_volume.busy;
    (void)kfree(fat16_volume.fat); memset(&fat16_volume, 0, sizeof fat16_volume); fat16_volume.busy = busy;
}
static const struct fs_operations operations = {
    .lookup = lookup, .stat = stat_node, .retain = retain, .release = release,
    .read = read_file, .readdir = readdir_node, .unmount = detach,
    .write = fat16_write_file, .replace = fat16_replace_file, .remove = fat16_remove_node,
    .mkdir = fat16_mkdir_node, .flush = fat16_flush, .truncate = fat16_truncate_file,
};

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
    fat16_volume.info = (struct fat16_information){ .sectors = total, .clusters = (uint32_t)clusters,
        .cluster_bytes = per_cluster * FAT16_SECTOR_SIZE, .fat_start = reserved, .fat_sectors = fat_sectors,
        .root_start = (uint32_t)root_start, .root_entries = entries, .data_start = (uint32_t)data_start };
    return FS_OK;
}
static enum fs_error mount_volume(struct block_device *device, bool read_only)
{
    if (fat16_volume.busy || fat16_volume.ready) return FS_BUSY;
    fat16_volume.busy = true;
    unsigned char buffer[FAT16_SECTOR_SIZE];
    enum fs_error error = geometry(device, buffer);
    if (error == FS_OK) {
        unsigned media = buffer[21];
        fat16_volume.device = device;
        size_t bytes = (size_t)fat16_volume.info.fat_sectors * FAT16_SECTOR_SIZE;
        fat16_volume.fat = kmalloc(bytes);
        if (!fat16_volume.fat) error = FS_NO_MEMORY;
        else error = fat16_read_sectors(fat16_volume.info.fat_start, fat16_volume.info.fat_sectors, fat16_volume.fat);
        for (uint32_t i = 0; error == FS_OK && i < fat16_volume.info.fat_sectors; ++i) {
            error = fat16_read_sectors(fat16_volume.info.fat_start + fat16_volume.info.fat_sectors + i, 1, buffer);
            if (error == FS_OK && memcmp(fat16_volume.fat + (size_t)i * FAT16_SECTOR_SIZE, buffer, FAT16_SECTOR_SIZE))
                error = FS_IO_ERROR;
        }
        if (error == FS_OK && (u16(fat16_volume.fat) != (0xff00u | media) ||
            (u16(fat16_volume.fat + 2) & 0x3fff) != 0x3fff)) error = FS_IO_ERROR;
        if (error == FS_OK) {
            fat16_volume.info.writable = !read_only && !device->read_only &&
                device->operations->write && device->operations->flush;
            const struct fs_backend backend = { .operations = &operations, .root = ROOT_ID,
                .naming = FS_NAMES_FAT83, .read_only = !fat16_volume.info.writable };
            fat16_volume.ready = true;
            error = fs_mount_disk(&backend);
            if (error == FS_OK) {
                uint32_t flags = cpu_interrupt_save(); fat16_volume.info.mounted = true; cpu_interrupt_restore(flags);
            }
        }
    }
    if (error != FS_OK) { (void)kfree(fat16_volume.fat); memset(&fat16_volume, 0, sizeof fat16_volume); }
    fat16_volume.busy = false; return error;
}
enum fs_error fat16_mount(struct block_device *device) { return mount_volume(device, false); }
enum fs_error fat16_mount_read_only(struct block_device *device) { return mount_volume(device, true); }
enum fs_error fat16_unmount(void)
{
    if (fat16_volume.busy) return FS_BUSY;
    if (!fat16_volume.info.mounted) return FS_UNAVAILABLE;
    fat16_volume.busy = true;
    enum fs_error error = fs_unmount_disk();
    fat16_volume.busy = false; return error;
}
struct fat16_information fat16_info(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct fat16_information info = fat16_volume.info.mounted ? fat16_volume.info : (struct fat16_information){0};
    cpu_interrupt_restore(flags); return info;
}
