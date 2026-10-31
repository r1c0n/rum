#include <rum/memory.h>
#include <rum/heap.h>
#include "test-files.h"
#include "fs-backend.h"

#define NODES 160u
#define BYTES 128u
struct node {
    uint64_t id, parent;
    char name[FS_NAME_CAPACITY];
    enum fs_kind kind;
    unsigned pins;
    size_t size;
    unsigned char data[BYTES];
    unsigned char *large;
    size_t capacity;
};
static struct node nodes[NODES];
static uint64_t next_id;
static unsigned calls, data_calls;
static bool initialized;
size_t fs_test_backend_short;
enum fs_error fs_test_backend_partial_error;
enum fs_error fs_test_backend_failure;
bool fs_test_backend_bad_cursor, fs_test_backend_reenter;
enum fs_error fs_test_backend_reentry_result;

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static struct node *by_id(uint64_t id)
{
    for (unsigned i = 0; i < NODES; ++i) if (nodes[i].id == id) return &nodes[i];
    return NULL;
}
static struct node *by_name(uint64_t parent, const char *name)
{
    /* Two names for one identity prove that retention is not pathname-based. */
    if (parent == 2 && equal(name, "ALIAS.TXT")) return by_id(4);
    for (unsigned i = 0; i < NODES; ++i)
        if (nodes[i].id && nodes[i].parent == parent && equal(nodes[i].name, name)) return &nodes[i];
    return NULL;
}
static struct fs_node describe(const struct node *node)
{
    return (struct fs_node){ node->id, node->size, node->kind };
}
static enum fs_error lookup(void *context, uint64_t parent, const char *name, struct fs_node *out)
{
    ++data_calls;
    (void)context; ++calls;
    struct node *directory = by_id(parent), *node = by_name(parent, name);
    if (!directory) return FS_NOT_FOUND;
    if (directory->kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    if (!node) return FS_NOT_FOUND;
    *out = describe(node); return FS_OK;
}
static enum fs_error stat_node(void *context, uint64_t id, struct fs_node *out)
{
    (void)context; ++calls;
    struct node *node = by_id(id);
    if (!node) return FS_NOT_FOUND;
    *out = describe(node); return FS_OK;
}
static enum fs_error retain(void *context, uint64_t id)
{
    (void)context; ++calls;
    if (fs_test_backend_failure != FS_OK) return fs_test_backend_failure;
    struct node *node = by_id(id);
    if (!node) return FS_NOT_FOUND;
    ++node->pins; return FS_OK;
}
static void release(void *context, uint64_t id)
{
    (void)context; ++calls;
    struct node *node = by_id(id);
    if (node && node->pins) --node->pins;
}
static struct fs_io_result transfer(uint64_t id, uint64_t offset, void *data, size_t bytes, bool write)
{
    ++data_calls;
    ++calls;
    if (fs_test_backend_reenter) {
        fs_reference ref;
        fs_test_backend_reentry_result = fs_open(NULL, "/", FS_READ, &ref);
    }
    if (fs_test_backend_failure != FS_OK)
        return (struct fs_io_result){ .error = fs_test_backend_failure, .device_status = 0x51, .device_error = 4 };
    struct node *node = by_id(id);
    if (!node) return (struct fs_io_result){ .error = FS_NOT_FOUND };
    if (node->kind != FS_FILE) return (struct fs_io_result){ .error = FS_IS_DIRECTORY };
    if (!write && offset >= node->size) return (struct fs_io_result){0};
    if (offset > node->size || (write && bytes > (node->large ? node->capacity : BYTES) - (size_t)offset))
        return (struct fs_io_result){ .error = FS_RANGE };
    if (!write && bytes > node->size - (size_t)offset) bytes = node->size - (size_t)offset;
    if (fs_test_backend_short && bytes > fs_test_backend_short) bytes = fs_test_backend_short;
    if (write) {
        memcpy((node->large ? node->large : node->data) + (size_t)offset, data, bytes);
        if (offset + bytes > node->size) node->size = (size_t)offset + bytes;
    } else memcpy(data, (node->large ? node->large : node->data) + (size_t)offset, bytes);
    return (struct fs_io_result){ .transferred = bytes, .error = fs_test_backend_partial_error };
}
static struct fs_io_result read_node(void *context, uint64_t id, uint64_t offset, void *data, size_t bytes)
{
    (void)context; return transfer(id, offset, data, bytes, false);
}
static struct fs_io_result write_node(void *context, uint64_t id, uint64_t offset, const void *data, size_t bytes)
{
    (void)context; return transfer(id, offset, (void *)data, bytes, true);
}
static enum fs_error put(uint64_t parent, const char *name, const void *data, size_t bytes, enum fs_kind kind)
{
    struct node *directory = by_id(parent);
    if (!directory) return FS_NOT_FOUND;
    if (directory->kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    if (bytes > RUM_ELF_IMAGE_LIMIT) return FS_NO_SPACE;
    struct node *node = by_name(parent, name);
    if (node && node->pins) return FS_BUSY;
    if (node && (node->kind == FS_DIRECTORY || kind == FS_DIRECTORY)) return FS_EXISTS;
    if (!node) {
        for (unsigned i = 0; i < NODES; ++i) if (!nodes[i].id) { node = &nodes[i]; break; }
        if (!node) return FS_NO_SPACE;
        *node = (struct node){ .id = next_id++, .parent = parent, .kind = kind };
        size_t length = 0; while (name[length]) ++length;
        memcpy(node->name, name, length + 1);
    }
    unsigned char *large = bytes > BYTES ? kmalloc(bytes) : NULL;
    if (bytes > BYTES && !large) return FS_NO_MEMORY;
    if (bytes) memcpy(large ? large : node->data, data, bytes);
    (void)kfree(node->large); node->large = large; node->capacity = bytes;
    node->size = bytes; return FS_OK;
}
static enum fs_error replace(void *context, uint64_t parent, const char *name, const void *data, size_t bytes)
{
    ++data_calls;
    (void)context; ++calls; return put(parent, name, data, bytes, FS_FILE);
}
static enum fs_error mkdir_node(void *context, uint64_t parent, const char *name)
{
    ++data_calls;
    (void)context; ++calls; return put(parent, name, NULL, 0, FS_DIRECTORY);
}
static enum fs_error remove_node(void *context, uint64_t parent, const char *name)
{
    ++data_calls;
    (void)context; ++calls;
    struct node *node = by_name(parent, name);
    if (!node) return FS_NOT_FOUND;
    if (node->pins) return FS_BUSY;
    for (unsigned i = 0; i < NODES; ++i)
        if (nodes[i].id && nodes[i].parent == node->id) return FS_NOT_EMPTY;
    (void)kfree(node->large); *node = (struct node){0}; return FS_OK;
}
static enum fs_error readdir_node(void *context, uint64_t id, uint64_t cursor,
                                  struct fs_entry *entry, uint64_t *next)
{
    ++data_calls;
    (void)context; ++calls;
    struct node *directory = by_id(id);
    if (!directory) return FS_NOT_FOUND;
    if (directory->kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    for (uint64_t i = cursor; i < NODES; ++i) {
        struct node *node = &nodes[i];
        if (!node->id || node->parent != id) continue;
        *entry = (struct fs_entry){ .kind = node->kind, .size = node->size };
        memcpy(entry->name, node->name, sizeof entry->name);
        *next = fs_test_backend_bad_cursor ? cursor : i + 1;
        return FS_OK;
    }
    return FS_END;
}
static enum fs_error flush(void *context) { (void)context; ++calls; ++data_calls; return fs_test_backend_failure; }
static const struct fs_operations operations = {
    lookup, stat_node, retain, release, read_node, write_node, replace, remove_node, mkdir_node, readdir_node, flush, NULL, NULL
};
static const struct fs_backend backend = { .operations = &operations, .root = 1, .naming = FS_NAMES_FAT83 };

void fs_test_backend_initialize(void)
{
    if (initialized) return;
    initialized = true;
    memset(nodes, 0, sizeof nodes); calls = 0; next_id = 2;
    fs_test_backend_failure = FS_OK; fs_test_backend_bad_cursor = fs_test_backend_reenter = false;
    fs_test_backend_short = 0; fs_test_backend_partial_error = FS_OK;
    nodes[0] = (struct node){ .id = 1, .kind = FS_DIRECTORY };
    (void)put(1, "DOCS", NULL, 0, FS_DIRECTORY); /* 2 */
    (void)put(2, "NEST", NULL, 0, FS_DIRECTORY); /* 3 */
    static const unsigned char data[] = {0, 'r', 0xff, 'u', 'm', '\n'};
    (void)put(2, "README.TXT", data, sizeof data, FS_FILE); /* 4 */
    (void)put(1, "ROOT.TXT", "root", 4, FS_FILE);
    (void)put(1, "EMPTY", NULL, 0, FS_DIRECTORY);
}
const struct fs_backend *fs_test_backend(void) { return &backend; }
unsigned fs_test_backend_calls(void) { return calls; }
unsigned fs_test_backend_data_calls(void) { return data_calls; }
unsigned fs_test_backend_pins(void)
{
    unsigned pins = 0;
    for (unsigned i = 0; i < NODES; ++i) pins += nodes[i].pins;
    return pins;
}

/* Fixture files share the mock disk namespace and use FAT-compatible spelling. */
static bool fixture_name(const char *input, char output[FS_NAME_CAPACITY])
{
    if (!input) return false;
    if (*input == '/') ++input;
    if (fs_name_check(FS_NAMES_FAT83, input) != FS_OK) return false;
    unsigned i = 0;
    for (; input[i]; ++i) output[i] = input[i] >= 'a' && input[i] <= 'z' ? input[i] - ('a' - 'A') : input[i];
    output[i] = 0; return true;
}
bool test_files_initialize(void) { fs_test_backend_initialize(); return fs_initialize(); }
bool test_files_mount(void) { return fs_mount_disk(&backend) == FS_OK; }
bool test_file_put(const char *name, const void *data, size_t bytes)
{
    char spelling[FS_NAME_CAPACITY];
    return fixture_name(name, spelling) && put(1, spelling, data, bytes, FS_FILE) == FS_OK;
}
bool test_file_read(const char *name, const unsigned char **data, size_t *bytes)
{
    char spelling[FS_NAME_CAPACITY];
    if (!fixture_name(name, spelling)) return false;
    struct node *node = by_name(1, spelling);
    if (!node || node->kind != FS_FILE) return false;
    if (data) *data = node->large ? node->large : node->data;
    if (bytes) *bytes = node->size;
    return true;
}
bool test_file_remove(const char *name)
{
    char spelling[FS_NAME_CAPACITY];
    return fixture_name(name, spelling) && remove_node(NULL, 1, spelling) == FS_OK;
}
struct test_file_statistics test_file_stats(void)
{
    struct test_file_statistics result = {0};
    for (unsigned i = 0; i < NODES; ++i) if (nodes[i].id && nodes[i].kind == FS_FILE) {
        ++result.files; result.bytes += nodes[i].size;
    }
    return result;
}
void test_file_list(test_file_visitor visitor, void *context)
{
    for (unsigned i = 0; i < NODES; ++i) if (nodes[i].id && nodes[i].kind == FS_FILE &&
        !visitor(nodes[i].name, nodes[i].size, context)) break;
}
