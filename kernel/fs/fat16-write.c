#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/path.h>
#include "fat16-internal.h"

/* Mutations run under the common filesystem's foreground serialization. The
   cached FAT is published only after both on-disk copies have been flushed. */
struct target {
    struct description directory, old;
    uint64_t id, cursor;
    unsigned char entry[ENTRY_BYTES];
    bool exists, end, grow, long_name;
};
static void put16(unsigned char *p, uint16_t n) { p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8); }
static void put32(unsigned char *p, uint32_t n) { put16(p, (uint16_t)n); put16(p + 2, (uint16_t)(n >> 16)); }
static void set_cluster(unsigned char *fat, uint32_t cluster, uint16_t next) { put16(fat + cluster * 2, next); }
static bool pinned(uint64_t id)
{
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i)
        if (fat16_volume.pins[i].count && fat16_volume.pins[i].id == id) return true;
    return false;
}
static enum fs_error entry_read(uint64_t id, unsigned char *entry)
{
    uint64_t offset;
    if (!fat16_slot_offset(id, &offset)) return FS_RANGE;
    enum fs_error error = fat16_sector((uint32_t)(offset / FAT16_SECTOR_SIZE));
    if (error == FS_OK) memcpy(entry, fat16_volume.sector + offset % FAT16_SECTOR_SIZE, ENTRY_BYTES);
    return error;
}
static enum fs_error finish(enum fs_error error, bool started)
{
    if (error != FS_OK && started) {
        uint32_t flags = cpu_interrupt_save();
        fat16_volume.info.faulted = true; fat16_volume.info.writable = false;
        cpu_interrupt_restore(flags);
        fat16_volume.cache_valid = false;
    }
    return error;
}
static enum fs_error device_result(struct block_result result, uint64_t expected)
{
    fat16_volume.last_io = (struct fs_io_result){ .error = fs_error_from_block(result.error),
        .device_status = result.status, .device_error = result.device_error };
    if (result.error == BLOCK_OK && result.completed != expected) fat16_volume.last_io.error = FS_IO_ERROR;
    return fat16_volume.last_io.error;
}
static enum fs_error write_sector(uint32_t sector, const void *data, bool *started)
{
    if (sector < fat16_volume.info.fat_start || sector >= fat16_volume.info.sectors) return FS_RANGE;
    if (fat16_volume.device->read_only) return FS_READ_ONLY;
    *started = true;
    fat16_volume.cache_valid = false;
    return device_result(block_write(fat16_volume.device, sector, 1, data, FAT16_SECTOR_SIZE), 1);
}
static enum fs_error flush_device(void)
{
    return device_result(block_flush(fat16_volume.device), 0);
}
static enum fs_error entry_write(uint64_t id, const unsigned char *entry, bool *started)
{
    uint64_t offset;
    if (!fat16_slot_offset(id, &offset)) return FS_RANGE;
    uint32_t number = (uint32_t)(offset / FAT16_SECTOR_SIZE);
    enum fs_error error = fat16_sector(number);
    if (error != FS_OK) return error;
    unsigned char data[FAT16_SECTOR_SIZE];
    memcpy(data, fat16_volume.sector, sizeof data);
    memcpy(data + offset % FAT16_SECTOR_SIZE, entry, ENTRY_BYTES);
    return write_sector(number, data, started);
}
static enum fs_error persist_fat(const unsigned char *fat, bool *started)
{
    for (unsigned copy = 0; copy < 2; ++copy) {
        bool changed = false;
        for (uint32_t i = 0; i < fat16_volume.info.fat_sectors; ++i) {
            size_t offset = (size_t)i * FAT16_SECTOR_SIZE;
            if (!memcmp(fat + offset, fat16_volume.fat + offset, FAT16_SECTOR_SIZE)) continue;
            enum fs_error error = write_sector(fat16_volume.info.fat_start + copy * fat16_volume.info.fat_sectors + i,
                                               fat + offset, started);
            if (error != FS_OK) return error;
            changed = true;
        }
        if (changed) {
            enum fs_error error = flush_device();
            if (error != FS_OK) return error;
        }
    }
    memcpy(fat16_volume.fat, fat, (size_t)fat16_volume.info.fat_sectors * FAT16_SECTOR_SIZE);
    return FS_OK;
}

/* Check ALL live short entries, including names outside rum's subset. Merely
   skipping an unsupported name must not hide its cluster ownership. */
static enum fs_error own_chain(uint32_t first, uint32_t length)
{
    uint32_t cluster = first;
    for (uint32_t i = 0; i < length; ++i) {
        unsigned char mask = (unsigned char)(1u << (cluster % 8));
        if (fat16_volume.owners[cluster / 8] & mask) return FS_IO_ERROR;
        fat16_volume.owners[cluster / 8] |= mask;
        cluster = fat16_next_cluster(cluster);
    }
    return FS_OK;
}
static bool dot(const unsigned char *entry, bool parent)
{
    if (entry[0] != '.' || entry[1] != (parent ? '.' : ' ')) return false;
    for (unsigned i = 2; i < 11; ++i) if (entry[i] != ' ') return false;
    return true;
}
static enum fs_error audit_directory(const struct description *directory, uint32_t parent,
                                     unsigned depth, uint32_t *budget)
{
    if (depth > FS_PATH_DEPTH) return FS_TOO_DEEP;
    uint64_t count = fat16_directory_slots(directory);
    for (uint64_t cursor = 0; cursor < count; ++cursor) {
        if (!*budget) return FS_UNSUPPORTED;
        --*budget;
        uint64_t id; unsigned char entry[ENTRY_BYTES];
        enum fs_error error = fat16_directory_slot(directory, cursor, &id);
        if (error == FS_OK) error = entry_read(id, entry);
        if (error != FS_OK) return error;
        bool sub = directory->node.id != ROOT_ID;
        if (sub && cursor < 2) {
            uint32_t expected = cursor ? parent : directory->first;
            if (!dot(entry, cursor != 0) || entry[11] != 0x10 || u16(entry + 20) ||
                u16(entry + 26) != expected || u32(entry + 28)) return FS_IO_ERROR;
            continue;
        }
        if (!entry[0]) return FS_OK;
        if (entry[0] == 0xe5 || entry[11] == 0x0f) continue;
        if (entry[11] & 8) {
            if (entry[11] != 8 || u16(entry + 20) || u16(entry + 26) || u32(entry + 28)) return FS_IO_ERROR;
            continue;
        }
        if (dot(entry, false) || dot(entry, true) || (entry[11] & 0xc0) || u16(entry + 20)) return FS_IO_ERROR;
        struct description child = { .node = { .id = id, .size = u32(entry + 28),
            .kind = entry[11] & 0x10 ? FS_DIRECTORY : FS_FILE }, .first = u16(entry + 26) };
        if (child.node.kind == FS_FILE && !child.node.size) {
            if (child.first) return FS_IO_ERROR;
            continue;
        }
        error = fat16_chain(child.first, &child.length);
        if (error != FS_OK) return error;
        uint64_t slots = (uint64_t)child.length * fat16_volume.info.cluster_bytes / ENTRY_BYTES;
        if (child.node.kind == FS_DIRECTORY) {
            if (child.node.size) return FS_IO_ERROR;
            if (slots > FAT16_DIRECTORY_ENTRY_LIMIT) return FS_UNSUPPORTED;
        } else if ((child.node.size + fat16_volume.info.cluster_bytes - 1) /
                    fat16_volume.info.cluster_bytes != child.length) return FS_IO_ERROR;
        error = own_chain(child.first, child.length);
        if (error != FS_OK) return error;
        if (child.node.kind == FS_DIRECTORY) {
            error = audit_directory(&child, sub ? directory->first : 0, depth + 1, budget);
            if (error != FS_OK) return error;
        }
    }
    return FS_OK;
}
static enum fs_error writable(void)
{
    if (!fat16_volume.ready) return FS_UNAVAILABLE;
    if (fat16_volume.info.faulted) return FS_IO_ERROR;
    if (!fat16_volume.info.writable || fat16_volume.device->read_only) return FS_READ_ONLY;
    if (!fat16_volume.audited) {
        memset(fat16_volume.owners, 0, sizeof fat16_volume.owners);
        struct description root = { .node = { .id = ROOT_ID, .kind = FS_DIRECTORY } };
        uint32_t budget = FAT16_WRITE_ENTRY_LIMIT;
        enum fs_error error = audit_directory(&root, 0, 0, &budget);
        if (error != FS_OK) return error;
        fat16_volume.audited = true;
    }
    return FS_OK;
}
static enum fs_error find_target(uint64_t parent, const char *name, struct target *target)
{
    memset(target, 0, sizeof *target);
    enum fs_error error = fat16_describe(parent, &target->directory);
    if (error != FS_OK) return error;
    if (target->directory.node.kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    uint64_t count = fat16_directory_slots(&target->directory), reusable = count;
    bool long_name = false, ended = false;
    for (uint64_t cursor = 0; cursor < count; ++cursor) {
        uint64_t id; unsigned char entry[ENTRY_BYTES];
        error = fat16_directory_slot(&target->directory, cursor, &id);
        if (error == FS_OK) error = entry_read(id, entry);
        if (error != FS_OK) return error;
        if (entry[0] == 0xe5 || !entry[0]) {
            if (long_name) return FS_UNSUPPORTED; /* Do not attach orphan LFN slots to a new name. */
            if (reusable == count) { reusable = cursor; target->id = id; target->end = !entry[0]; }
            long_name = false;
            if (!entry[0]) { ended = true; break; }
            continue;
        }
        if (entry[11] == 0x0f) { long_name = true; continue; }
        struct description old; char decoded[FS_NAME_CAPACITY];
        error = fat16_decode(entry, id, &old, decoded);
        if (error == FS_OK && equal(decoded, name)) {
            target->old = old; target->exists = true; target->id = id; target->cursor = cursor;
            target->long_name = long_name; memcpy(target->entry, entry, ENTRY_BYTES); return FS_OK;
        }
        if (error != FS_OK && error != FS_NOT_FOUND) return error;
        long_name = false;
    }
    target->cursor = reusable;
    target->grow = reusable == count && !ended;
    if (target->grow && parent == ROOT_ID) return FS_NO_SPACE;
    if (target->grow && count + fat16_volume.info.cluster_bytes / ENTRY_BYTES > FAT16_DIRECTORY_ENTRY_LIMIT)
        return FS_NO_SPACE;
    return FS_OK;
}
static unsigned char *fat_copy(void)
{
    size_t bytes = (size_t)fat16_volume.info.fat_sectors * FAT16_SECTOR_SIZE;
    unsigned char *copy = kmalloc(bytes);
    if (copy) memcpy(copy, fat16_volume.fat, bytes);
    return copy;
}
static enum fs_error allocate(unsigned char *fat, uint32_t count, uint32_t *first)
{
    *first = 0;
    uint32_t last = 0;
    for (uint32_t cluster = 2; cluster <= fat16_volume.info.clusters + 1 && count; ++cluster) {
        if (!fat16_valid_cluster(cluster) || u16(fat + cluster * 2)) continue;
        if (last) set_cluster(fat, last, (uint16_t)cluster);
        else *first = cluster;
        last = cluster; set_cluster(fat, cluster, 0xffff); --count;
    }
    return count ? FS_NO_SPACE : FS_OK;
}
static void free_chain(unsigned char *fat, const struct description *old)
{
    uint32_t cluster = old->first;
    for (uint32_t i = 0; i < old->length; ++i) {
        uint32_t next = fat16_next_cluster(cluster);
        set_cluster(fat, cluster, 0); cluster = next;
    }
}
static void make_entry(unsigned char *entry, const char *name, bool directory, uint32_t first, uint32_t size)
{
    memset(entry, 0, ENTRY_BYTES); memset(entry, ' ', 11);
    unsigned offset = 0;
    for (; *name; ++name) {
        if (*name == '.') offset = 8;
        else entry[offset++] = (unsigned char)*name;
    }
    entry[11] = directory ? 0x10 : 0x20;
    put16(entry + 16, 0x21); put16(entry + 18, 0x21); put16(entry + 24, 0x21); /* 1980-01-01, no RTC yet. */
    put16(entry + 26, (uint16_t)first); put32(entry + 28, size);
}
static enum fs_error zero_cluster(uint32_t cluster, uint32_t self, uint32_t parent, bool *started)
{
    unsigned char data[FAT16_SECTOR_SIZE] = {0};
    uint32_t sectors = fat16_volume.info.cluster_bytes / FAT16_SECTOR_SIZE;
    if (self) {
        memset(data, ' ', 11); data[0] = '.'; data[11] = 0x10; put16(data + 26, (uint16_t)self);
        memset(data + ENTRY_BYTES, ' ', 11); data[ENTRY_BYTES] = '.'; data[ENTRY_BYTES + 1] = '.';
        data[ENTRY_BYTES + 11] = 0x10; put16(data + ENTRY_BYTES + 26, (uint16_t)parent);
        for (unsigned slot = 0; slot < 2; ++slot) {
            unsigned char *entry = data + slot * ENTRY_BYTES;
            put16(entry + 16, 0x21); put16(entry + 18, 0x21); put16(entry + 24, 0x21);
        }
    }
    for (uint32_t i = 0; i < sectors; ++i) {
        enum fs_error error = write_sector(fat16_cluster_sector(cluster) + i, data, started);
        if (error != FS_OK) return error;
        if (!i) memset(data, 0, sizeof data);
    }
    return FS_OK;
}
static enum fs_error grow_directory(struct target *target, unsigned char *fat, bool *started)
{
    if (!target->grow) return FS_OK;
    uint32_t cluster;
    enum fs_error error = allocate(fat, 1, &cluster);
    if (error != FS_OK) return error;
    uint32_t last = fat16_cluster_at(&target->directory, target->directory.length - 1);
    set_cluster(fat, last, (uint16_t)cluster);
    target->id = (uint64_t)fat16_cluster_sector(cluster) * ENTRIES_PER_SECTOR + 2;
    return zero_cluster(cluster, 0, 0, started);
}
static enum fs_error publish(struct target *target, const unsigned char *entry, bool *started)
{
    /* A consumed end marker must keep the next slot an end marker, even when
       the host left stale bytes beyond the original logical directory end. */
    if (target->end && target->cursor + 1 < fat16_directory_slots(&target->directory)) {
        uint64_t next; unsigned char following[ENTRY_BYTES];
        enum fs_error error = fat16_directory_slot(&target->directory, target->cursor + 1, &next);
        if (error == FS_OK) error = entry_read(next, following);
        if (error != FS_OK) return error;
        if (following[0]) {
            memset(following, 0, sizeof following);
            error = entry_write(next, following, started);
            if (error == FS_OK) error = flush_device();
            if (error != FS_OK) return error;
        }
    }
    enum fs_error error = entry_write(target->id, entry, started);
    return error == FS_OK ? flush_device() : error;
}
static enum fs_error copy_contents(const struct description *old, const unsigned char *fat, uint32_t first,
                                  uint32_t size, uint64_t patch_offset, const void *patch, size_t bytes,
                                  bool keep, bool *started)
{
    uint32_t cluster = first, index = 0, old_cluster = keep ? old->first : 0;
    uint32_t sectors = fat16_volume.info.cluster_bytes / FAT16_SECTOR_SIZE;
    for (uint64_t base = 0; base < size; ++index) {
        for (uint32_t i = 0; i < sectors; ++i, base += FAT16_SECTOR_SIZE) {
            unsigned char data[FAT16_SECTOR_SIZE] = {0};
            if (keep && base < size && base < old->node.size) {
                enum fs_error error = fat16_sector(fat16_cluster_sector(old_cluster) + i);
                if (error != FS_OK) return error;
                size_t amount = (size_t)(old->node.size - base);
                if (amount > sizeof data) amount = sizeof data;
                if (amount > size - base) amount = (size_t)(size - base);
                memcpy(data, fat16_volume.sector, amount);
            }
            uint64_t start = base > patch_offset ? base : patch_offset;
            uint64_t end = base + FAT16_SECTOR_SIZE;
            if (end > patch_offset + bytes) end = patch_offset + bytes;
            if (end > size) end = size;
            if (end > start) memcpy(data + (size_t)(start - base), (const unsigned char *)patch +
                                    (size_t)(start - patch_offset), (size_t)(end - start));
            enum fs_error error = write_sector(fat16_cluster_sector(cluster) + i, data, started);
            if (error != FS_OK) return error;
        }
        cluster = u16(fat + cluster * 2);
        if (keep && index + 1 < old->length) old_cluster = fat16_next_cluster(old_cluster);
    }
    return FS_OK;
}
static enum fs_error rewrite(struct target *target, uint64_t size, uint64_t offset, const void *data,
                             size_t bytes, bool keep, bool *committed)
{
    *committed = false;
    if (size > UINT32_MAX || size > (uint64_t)fat16_volume.info.clusters * fat16_volume.info.cluster_bytes)
        return FS_RANGE;
    if (target->exists && (target->entry[11] & 1)) return FS_ACCESS;
    uint32_t needed = (uint32_t)((size + fat16_volume.info.cluster_bytes - 1) / fat16_volume.info.cluster_bytes);
    unsigned char *fat = fat_copy();
    if (!fat) return FS_NO_MEMORY;
    uint32_t first;
    bool started = false;
    enum fs_error error = allocate(fat, needed, &first);
    /* Reserve directory growth before writing any file data. */
    if (error == FS_OK && target->grow) {
        uint32_t free_count = 0;
        for (uint32_t c = 2; c <= fat16_volume.info.clusters + 1; ++c)
            if (fat16_valid_cluster(c) && !u16(fat + c * 2)) ++free_count;
        if (!free_count) error = FS_NO_SPACE;
    }
    if (error == FS_OK) error = copy_contents(&target->old, fat, first, (uint32_t)size, offset, data, bytes, keep, &started);
    if (error == FS_OK) error = grow_directory(target, fat, &started);
    if (error == FS_OK) error = flush_device();
    if (error == FS_OK) error = persist_fat(fat, &started);
    unsigned char entry[ENTRY_BYTES]; memcpy(entry, target->entry, sizeof entry);
    put16(entry + 26, (uint16_t)first); put32(entry + 28, (uint32_t)size); entry[11] |= 0x20;
    if (error == FS_OK) error = publish(target, entry, &started);
    if (error == FS_OK) {
        *committed = true;
        free_chain(fat, &target->old);
        error = persist_fat(fat, &started);
    }
    (void)kfree(fat);
    return finish(error, started);
}
enum fs_error fat16_replace_file(void *context, uint64_t parent, const char *name, const void *data, size_t bytes)
{
    (void)context;
    enum fs_error error = writable();
    if (error != FS_OK) return error;
    struct target target;
    error = find_target(parent, name, &target);
    if (error != FS_OK) return error;
    if (target.exists && target.old.node.kind != FS_FILE) return FS_IS_DIRECTORY;
    if (target.exists && pinned(target.id)) return FS_BUSY;
    if (!target.exists) make_entry(target.entry, name, false, 0, 0);
    bool committed;
    return rewrite(&target, bytes, 0, data, bytes, false, &committed);
}
static enum fs_error by_id(uint64_t id, struct target *target)
{
    memset(target, 0, sizeof *target);
    enum fs_error error = fat16_describe(id, &target->old);
    if (error != FS_OK) return error;
    if (target->old.node.kind != FS_FILE) return FS_IS_DIRECTORY;
    target->id = id; target->exists = true;
    return entry_read(id, target->entry);
}
struct fs_io_result fat16_write_file(void *context, uint64_t id, uint64_t offset, const void *data, size_t bytes)
{
    (void)context;
    fat16_volume.last_io = (struct fs_io_result){0};
    struct fs_io_result result = { .error = writable() };
    struct target target;
    if (result.error == FS_OK) result.error = by_id(id, &target);
    if (result.error == FS_OK && (offset > target.old.node.size || bytes > UINT32_MAX || offset > UINT32_MAX - bytes))
        result.error = FS_RANGE;
    bool committed = false;
    if (result.error == FS_OK) {
        uint64_t size = offset + bytes;
        if (size < target.old.node.size) size = target.old.node.size;
        result.error = rewrite(&target, size, offset, data, bytes, true, &committed);
    }
    result.transferred = committed ? bytes : 0;
    result.device_status = fat16_volume.last_io.device_status;
    result.device_error = fat16_volume.last_io.device_error;
    return result;
}
enum fs_error fat16_truncate_file(void *context, uint64_t id, uint64_t size)
{
    (void)context;
    enum fs_error error = writable();
    struct target target;
    if (error == FS_OK) error = by_id(id, &target);
    if (error != FS_OK) return error;
    if (target.entry[11] & 1) return FS_ACCESS;
    if (size == target.old.node.size) return FS_OK;
    bool committed;
    return rewrite(&target, size, 0, NULL, 0, true, &committed);
}
enum fs_error fat16_mkdir_node(void *context, uint64_t parent, const char *name)
{
    (void)context;
    enum fs_error error = writable();
    if (error != FS_OK) return error;
    struct target target;
    error = find_target(parent, name, &target);
    if (error != FS_OK) return error;
    if (target.exists) return FS_EXISTS;
    unsigned char *fat = fat_copy();
    if (!fat) return FS_NO_MEMORY;
    uint32_t cluster = 0;
    bool started = false;
    error = allocate(fat, target.grow ? 2 : 1, &cluster);
    /* Two reservations are linked temporarily; separate them before publishing. */
    uint32_t extra = target.grow && error == FS_OK ? u16(fat + cluster * 2) : 0;
    if (error == FS_OK) {
        set_cluster(fat, cluster, 0xffff);
        error = zero_cluster(cluster, cluster, parent == ROOT_ID ? 0 : target.directory.first, &started);
    }
    if (error == FS_OK && target.grow) {
        set_cluster(fat, fat16_cluster_at(&target.directory, target.directory.length - 1), (uint16_t)extra);
        target.id = (uint64_t)fat16_cluster_sector(extra) * ENTRIES_PER_SECTOR + 2;
        error = zero_cluster(extra, 0, 0, &started);
    }
    if (error == FS_OK) error = flush_device();
    if (error == FS_OK) error = persist_fat(fat, &started);
    unsigned char entry[ENTRY_BYTES]; make_entry(entry, name, true, cluster, 0);
    if (error == FS_OK) error = publish(&target, entry, &started);
    (void)kfree(fat);
    return finish(error, started);
}
enum fs_error fat16_remove_node(void *context, uint64_t parent, const char *name)
{
    (void)context;
    enum fs_error error = writable();
    if (error != FS_OK) return error;
    struct target target;
    error = find_target(parent, name, &target);
    if (error != FS_OK) return error;
    if (!target.exists) return FS_NOT_FOUND;
    if (pinned(target.id)) return FS_BUSY;
    if (target.entry[11] & 1) return FS_ACCESS;
    if (target.long_name) return FS_UNSUPPORTED; /* Preserve associated LFN slots. */
    if (target.old.node.kind == FS_DIRECTORY) {
        for (uint64_t i = 2; i < fat16_directory_slots(&target.old); ++i) {
            uint64_t id; unsigned char entry[ENTRY_BYTES];
            error = fat16_directory_slot(&target.old, i, &id);
            if (error == FS_OK) error = entry_read(id, entry);
            if (error != FS_OK) return error;
            if (!entry[0]) break;
            if (entry[0] != 0xe5) return FS_NOT_EMPTY;
        }
    }
    unsigned char *fat = fat_copy();
    if (!fat) return FS_NO_MEMORY;
    bool started = false;
    target.entry[0] = 0xe5;
    error = entry_write(target.id, target.entry, &started);
    if (error == FS_OK) error = flush_device();
    if (error == FS_OK) { free_chain(fat, &target.old); error = persist_fat(fat, &started); }
    (void)kfree(fat);
    return finish(error, started);
}
enum fs_error fat16_flush(void *context)
{
    (void)context;
    if (!fat16_volume.ready) return FS_UNAVAILABLE;
    if (fat16_volume.info.faulted) return FS_IO_ERROR;
    if (!fat16_volume.info.writable) return FS_OK;
    return finish(flush_device(), true);
}
