#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/ramfs.h>

struct file {
    struct file *next;
    size_t size;
    unsigned char *data;
    char name[RAMFS_NAME_CAPACITY];
};
static struct file *ramfs_first, *ramfs_last;
static struct ramfs_statistics statistics;
static bool ready;

static const char *normalize(const char *name)
{
    if (!name) return NULL;
    if (*name == '/') ++name;
    size_t length = 0;
    while (name[length]) {
        unsigned char c = name[length];
        if (++length >= RAMFS_NAME_CAPACITY ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return NULL;
    }
    if (!length || (length == 1 && name[0] == '.') ||
        (length == 2 && name[0] == '.' && name[1] == '.')) return NULL;
    return name;
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

bool ramfs_initialize(void)
{
    if (ready || !heap_stats().mapped_bytes) return false;
    ready = true;
    return true;
}

bool ramfs_put(const char *name, const void *data, size_t size)
{
    if (!ready || !(name = normalize(name)) || size > RAMFS_FILE_LIMIT ||
        (size && !data)) return false;
    struct file *file = find(name);
    bool creating = file == NULL;
    if (creating) {
        if (statistics.files == RAMFS_MAX_FILES) return false;
        file = kcalloc(1, sizeof *file);
        if (!file) return false;
        size_t length = 0;
        do { file->name[length] = name[length]; } while (name[length++]);
    }
    unsigned char *copy = size ? kmalloc(size) : NULL;
    if (size && !copy) { if (creating) (void)kfree(file); return false; }
    if (size) memcpy(copy, data, size);
    /* Commit after all allocations/copying, including writes from borrowed data. */
    statistics.bytes = statistics.bytes - file->size + size;
    (void)kfree(file->data);
    file->data = copy;
    file->size = size;
    if (creating) {
        if (ramfs_last) ramfs_last->next = file;
        else ramfs_first = file;
        ramfs_last = file;
        ++statistics.files;
    }
    return true;
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

bool ramfs_remove(const char *name)
{
    if (!ready || !(name = normalize(name))) return false;
    struct file *previous = NULL;
    for (struct file *file = ramfs_first; file; previous = file, file = file->next) {
        if (!equal(file->name, name)) continue;
        if (previous) previous->next = file->next;
        else ramfs_first = file->next;
        if (ramfs_last == file) ramfs_last = previous;
        --statistics.files;
        statistics.bytes -= file->size;
        (void)kfree(file->data);
        (void)kfree(file);
        return true;
    }
    return false;
}

void ramfs_list(ramfs_visitor visitor, void *context)
{
    if (!visitor) return;
    for (struct file *file = ramfs_first; file; file = file->next)
        if (!visitor(file->name, file->size, context)) break;
}

struct ramfs_statistics ramfs_stats(void)
{
    return statistics;
}
