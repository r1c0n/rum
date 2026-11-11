#include <rum/fs_policy.h>
#include <rum/path.h>
#include <rum/memory.h>

static bool (*system_authority)(void), (*device_authority)(void);
bool fs_set_authority_policy(bool (*system_write)(void), bool (*device_control)(void))
{
    if (!system_write || !device_control) return false;
    if (system_authority || device_authority)
        return system_authority == system_write && device_authority == device_control;
    system_authority = system_write; device_authority = device_control; return true;
}
/* No userspace exists before the task manager installs its immutable policy. */
bool fs_system_write_allowed(void) { return !system_authority || system_authority(); }
bool fs_device_control_allowed(void) { return !device_authority || device_authority(); }
bool fs_system_identity(const struct fs_system_index *index, uint64_t id)
{
    for (unsigned i = 0; i < index->count; ++i) if (index->ids[i] == id) return true;
    return false;
}
/* The caller holds the synchronous namespace operation. Backend callbacks
   cannot reenter it. Fixed storage bounds both memory and directory traversal. */
static struct { uint64_t id; unsigned depth; } directories[FS_SYSTEM_OBJECT_LIMIT];
enum fs_error fs_system_index_build(struct fs_system_index *index, const struct fs_backend *backend)
{
    index->valid = false; index->count = 0;
    struct fs_node node = {0};
    enum fs_error error = backend->operations->lookup(backend->context, backend->root, "RUM", &node);
    if (error == FS_NOT_FOUND) { index->valid = true; return FS_OK; }
    if (error != FS_OK) return error;
    if (!node.id || (node.kind != FS_FILE && node.kind != FS_DIRECTORY)) return FS_IO_ERROR;
    index->ids[index->count++] = node.id;
    unsigned head = 0, tail = 0, entries = 0;
    if (node.kind == FS_DIRECTORY) directories[tail++] = (typeof(directories[0])){node.id, 1};
    while (head < tail) {
        uint64_t id = directories[head].id, cursor = 0;
        unsigned depth = directories[head++].depth;
        for (;;) {
            struct fs_entry entry = {0}; uint64_t next = 0;
            error = backend->operations->readdir(backend->context, id, cursor, &entry, &next);
            if (error == FS_END) break;
            if (error != FS_OK) return error;
            if (next <= cursor || fs_name_check(backend->naming, entry.name) != FS_OK) return FS_IO_ERROR;
            if (++entries >= FS_SYSTEM_OBJECT_LIMIT) return FS_NO_SPACE;
            cursor = next;
            error = backend->operations->lookup(backend->context, id, entry.name, &node);
            if (error != FS_OK) return error;
            if (!node.id || (node.kind != FS_FILE && node.kind != FS_DIRECTORY) || node.kind != entry.kind) return FS_IO_ERROR;
            if (fs_system_identity(index, node.id)) continue;
            if (index->count == FS_SYSTEM_OBJECT_LIMIT) return FS_NO_SPACE;
            index->ids[index->count++] = node.id;
            if (node.kind == FS_DIRECTORY) {
                if (depth == FS_PATH_DEPTH) return FS_TOO_DEEP;
                directories[tail++] = (typeof(directories[0])){node.id, depth + 1};
            }
        }
    }
    index->valid = true; return FS_OK;
}
