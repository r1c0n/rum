#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/path.h>
#include <rum/ramfs.h>

#define ROOT_ID UINT64_C(1)
struct file {
    struct file *next;
    uint64_t id;
    uint32_t references;
    size_t size;
    unsigned char *data;
    char name[RAMFS_NAME_CAPACITY];
};
#ifdef __i386__
_Static_assert(sizeof(struct file) == 88 && offsetof(struct file, name) == 24,
               "i386 RAM node layout audited by QEMU smoke tests");
#endif
static struct file *ramfs_first, *ramfs_last;
static struct ramfs_statistics statistics;
static uint64_t next_id = ROOT_ID + 1;
static bool ready, disk_reserved;

static const char *normalize(const char *name)
{
    if (!name) return NULL;
    if (*name == '/') ++name;
    return fs_name_check(FS_NAMES_RAM, name) == FS_OK ? name : NULL;
}

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static struct file *find(const char *name)
{
    for (struct file *file = ramfs_first; file; file = file->next)
        if (equal(name, file->name)) return file;
    return NULL;
}

static struct file *find_id(uint64_t id)
{
    for (struct file *file = ramfs_first; file; file = file->next)
        if (file->id == id) return file;
    return NULL;
}

static struct fs_node node(const struct file *file)
{
    return (struct fs_node){ .id = file->id, .size = file->size, .kind = FS_FILE };
}

bool ramfs_initialize(void)
{
    if (ready || !heap_stats().mapped_bytes) return false;
    ready = true;
    return true;
}

static enum fs_error replace(void *context, uint64_t parent, const char *name,
                              const void *data, size_t size)
{
    (void)context;
    if (!ready) return FS_UNAVAILABLE;
    if (parent != ROOT_ID) return FS_NOT_DIRECTORY;
    if (!(name = normalize(name)) || (size && !data)) return FS_INVALID;
    if (size > RAMFS_FILE_LIMIT) return FS_RANGE;
    if (disk_reserved && equal(name, "disk")) return FS_BUSY;
    struct file *file = find(name);
    bool creating = !file;
    if (file && file->references) return FS_BUSY;
    if (creating) {
        if (statistics.files == RAMFS_MAX_FILES || !next_id) return FS_NO_SPACE;
        file = kcalloc(1, sizeof *file);
        if (!file) return FS_NO_MEMORY;
        size_t length = 0;
        do { file->name[length] = name[length]; } while (name[length++]);
    }
    unsigned char *copy = size ? kmalloc(size) : NULL;
    if (size && !copy) { if (creating) (void)kfree(file); return FS_NO_MEMORY; }
    if (size) memcpy(copy, data, size);
    /* Commit after all allocations/copying, including borrowed-byte writes. */
    statistics.bytes = statistics.bytes - file->size + size;
    (void)kfree(file->data);
    file->data = copy; file->size = size;
    if (creating) {
        file->id = next_id++;
        if (ramfs_last) ramfs_last->next = file;
        else ramfs_first = file;
        ramfs_last = file; ++statistics.files;
    }
    return FS_OK;
}

bool ramfs_put(const char *name, const void *data, size_t size)
{
    return replace(NULL, ROOT_ID, name, data, size) == FS_OK;
}

bool ramfs_read(const char *name, const unsigned char **data, size_t *size)
{
    if (!ready || !(name = normalize(name))) return false;
    struct file *file = find(name);
    if (!file) return false;
    if (data) *data = file->data;
    if (size) *size = file->size;
    return true;
}

static enum fs_error remove_file(void *context, uint64_t parent, const char *name)
{
    (void)context;
    if (!ready) return FS_UNAVAILABLE;
    if (parent != ROOT_ID) return FS_NOT_DIRECTORY;
    if (!(name = normalize(name))) return FS_INVALID;
    struct file *previous = NULL;
    for (struct file *file = ramfs_first; file; previous = file, file = file->next) {
        if (!equal(file->name, name)) continue;
        if (file->references) return FS_BUSY;
        if (previous) previous->next = file->next;
        else ramfs_first = file->next;
        if (ramfs_last == file) ramfs_last = previous;
        --statistics.files; statistics.bytes -= file->size;
        (void)kfree(file->data); (void)kfree(file);
        return FS_OK;
    }
    return FS_NOT_FOUND;
}

bool ramfs_remove(const char *name) { return remove_file(NULL, ROOT_ID, name) == FS_OK; }

void ramfs_list(ramfs_visitor visitor, void *context)
{
    if (!visitor) return;
    for (struct file *file = ramfs_first; file; file = file->next)
        if (!visitor(file->name, file->size, context)) break;
}

struct ramfs_statistics ramfs_stats(void) { return statistics; }
void ramfs_reserve_disk(bool reserved) { disk_reserved = reserved; }

static enum fs_error lookup(void *context, uint64_t parent, const char *name, struct fs_node *result)
{
    (void)context;
    if (!ready) return FS_UNAVAILABLE;
    if (parent != ROOT_ID) return FS_NOT_DIRECTORY;
    struct file *file = find(name);
    if (!file) return FS_NOT_FOUND;
    *result = node(file); return FS_OK;
}

static enum fs_error stat(void *context, uint64_t id, struct fs_node *result)
{
    (void)context;
    if (id == ROOT_ID) { *result = (struct fs_node){ .id = ROOT_ID, .kind = FS_DIRECTORY }; return FS_OK; }
    if (!ready) return FS_UNAVAILABLE;
    struct file *file = find_id(id);
    if (!file) return FS_NOT_FOUND;
    *result = node(file); return FS_OK;
}

static enum fs_error retain(void *context, uint64_t id)
{
    (void)context;
    if (id == ROOT_ID) return FS_OK;
    struct file *file = find_id(id);
    if (!file) return FS_NOT_FOUND;
    if (file->references == UINT32_MAX) return FS_NO_SPACE;
    ++file->references; return FS_OK;
}

static void release(void *context, uint64_t id)
{
    (void)context;
    struct file *file = find_id(id);
    if (file && file->references) --file->references;
}

static struct fs_io_result read_at(void *context, uint64_t id, uint64_t offset, void *data, size_t bytes)
{
    (void)context;
    struct file *file = find_id(id);
    if (!file) return (struct fs_io_result){ .error = FS_NOT_FOUND };
    if (offset >= file->size) return (struct fs_io_result){0};
    size_t available = file->size - (size_t)offset;
    if (bytes > available) bytes = available;
    if (bytes) memcpy(data, file->data + (size_t)offset, bytes);
    return (struct fs_io_result){ .transferred = bytes };
}

static struct fs_io_result write_at(void *context, uint64_t id, uint64_t offset, const void *data, size_t bytes)
{
    (void)context;
    struct file *file = find_id(id);
    if (!file) return (struct fs_io_result){ .error = FS_NOT_FOUND };
    if (offset > file->size || offset > RAMFS_FILE_LIMIT || bytes > RAMFS_FILE_LIMIT - offset)
        return (struct fs_io_result){ .error = FS_RANGE };
    size_t end = (size_t)offset + bytes;
    if (end <= file->size) {
        if (bytes) memmove(file->data + (size_t)offset, data, bytes);
    } else {
        unsigned char *copy = kmalloc(end);
        if (!copy) return (struct fs_io_result){ .error = FS_NO_MEMORY };
        if (file->size) memcpy(copy, file->data, file->size);
        if (bytes) memcpy(copy + (size_t)offset, data, bytes);
        statistics.bytes += end - file->size;
        (void)kfree(file->data); file->data = copy; file->size = end;
    }
    return (struct fs_io_result){ .transferred = bytes };
}

static enum fs_error readdir(void *context, uint64_t id, uint64_t cursor,
                              struct fs_entry *entry, uint64_t *next)
{
    (void)context;
    if (id != ROOT_ID) return FS_NOT_DIRECTORY;
    if (!ready) return FS_UNAVAILABLE;
    struct file *file = ramfs_first;
    for (uint64_t i = 0; file && i < cursor; ++i) file = file->next;
    if (!file) return FS_END;
    *entry = (struct fs_entry){ .kind = FS_FILE, .size = file->size };
    size_t length = 0;
    do { entry->name[length] = file->name[length]; } while (file->name[length++]);
    *next = cursor + 1; return FS_OK;
}

static enum fs_error flush(void *context) { (void)context; return FS_OK; }
static enum fs_error truncate_file(void *context, uint64_t id, uint64_t size)
{
    (void)context;
    struct file *file = find_id(id);
    if (!file) return FS_NOT_FOUND;
    if (size > RAMFS_FILE_LIMIT) return FS_RANGE;
    if (size == file->size) return FS_OK;
    unsigned char *copy = size ? kmalloc((size_t)size) : NULL;
    if (size && !copy) return FS_NO_MEMORY;
    size_t keep = size < file->size ? (size_t)size : file->size;
    if (keep) memcpy(copy, file->data, keep);
    if (size > keep) memset(copy + keep, 0, (size_t)size - keep);
    statistics.bytes = statistics.bytes - file->size + (size_t)size;
    (void)kfree(file->data); file->data = copy; file->size = (size_t)size;
    return FS_OK;
}
static const struct fs_operations operations = {
    .lookup = lookup, .stat = stat, .retain = retain, .release = release,
    .read = read_at, .write = write_at, .replace = replace, .remove = remove_file,
    .readdir = readdir, .flush = flush, .truncate = truncate_file,
};
static const struct fs_backend backend = { .operations = &operations, .root = ROOT_ID, .naming = FS_NAMES_RAM };
const struct fs_backend *ramfs_backend(void) { return &backend; }
