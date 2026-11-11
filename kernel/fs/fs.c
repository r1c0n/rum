#include <rum/cpu.h>
#include <rum/fs.h>
#include <rum/memory.h>
#include <rum/path.h>
#include <rum/fs_policy.h>

struct mount { struct fs_backend backend; struct fs_system_index system; uint32_t generation; bool mounted; };
struct object { uint64_t id; uint32_t references; enum fs_mount_id mount; enum fs_kind kind; bool system; };
struct reference { fs_reference token; unsigned object, access; };
struct location { enum fs_mount_id mount; unsigned depth; struct fs_node nodes[FS_PATH_DEPTH + 1]; };
static struct mount mounts[FS_MOUNT_COUNT];
static struct object objects[FS_OBJECT_LIMIT];
static struct reference references[FS_REFERENCE_LIMIT];
static fs_reference next_token = 1;
static bool ready, busy;

bool fs_operation_active(void)
{
    uint32_t flags = cpu_interrupt_save();
    bool active = busy;
    cpu_interrupt_restore(flags);
    return active;
}

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static enum fs_error enter(void)
{
    uint32_t flags = cpu_interrupt_save();
    enum fs_error error = !ready ? FS_UNAVAILABLE : busy ? FS_BUSY : FS_OK;
    if (error == FS_OK) busy = true;
    cpu_interrupt_restore(flags);
    return error;
}

static void leave(void)
{
    uint32_t flags = cpu_interrupt_save(); busy = false; cpu_interrupt_restore(flags);
}

bool fs_initialize(void)
{
    if (ready) return true;
    ready = true;
    return true;
}

struct fs_statistics fs_stats(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct fs_statistics stats = {0};
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i) stats.objects += objects[i].references != 0;
    for (unsigned i = 0; i < FS_REFERENCE_LIMIT; ++i) stats.references += references[i].token != 0;
    for (unsigned i = 0; i < FS_MOUNT_COUNT; ++i) stats.mounts += mounts[i].mounted;
    cpu_interrupt_restore(flags);
    return stats;
}

static bool valid_node(const struct fs_node *node)
{
    return node->id && (node->kind == FS_FILE || node->kind == FS_DIRECTORY);
}

static enum fs_error root(struct location *location, enum fs_mount_id mount)
{
    if (!mounts[mount].mounted) return FS_UNAVAILABLE;
    struct fs_backend *backend = &mounts[mount].backend;
    *location = (struct location){ .mount = mount };
    enum fs_error error = backend->operations->stat(backend->context, backend->root, &location->nodes[0]);
    if (error == FS_OK && (!valid_node(&location->nodes[0]) ||
        location->nodes[0].id != backend->root || location->nodes[0].kind != FS_DIRECTORY)) return FS_IO_ERROR;
    return error;
}

static enum fs_error step(struct location *location, const char *name)
{
    struct fs_node *parent = &location->nodes[location->depth];
    if (parent->kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    if (equal(name, ".")) return FS_OK;
    if (equal(name, "..")) {
        if (location->depth) --location->depth;
        return FS_OK;
    }
    if (location->depth == FS_PATH_DEPTH) return FS_TOO_DEEP;
    struct fs_backend *backend = &mounts[location->mount].backend;
    struct fs_node node = {0};
    enum fs_error error = backend->operations->lookup(backend->context, parent->id, name, &node);
    if (error == FS_OK) {
        if (!valid_node(&node)) return FS_IO_ERROR;
        location->nodes[++location->depth] = node;
    }
    return error;
}

static struct reference *find_reference(fs_reference token)
{
    if (token) for (unsigned i = 0; i < FS_REFERENCE_LIMIT; ++i)
        if (references[i].token == token) return &references[i];
    return NULL;
}

static bool system_denied(enum fs_mount_id mount, uint64_t id)
{
    return !fs_system_write_allowed() && (!mounts[mount].system.valid || fs_system_identity(&mounts[mount].system, id));
}
static bool location_denied(const struct location *location)
{
    for (unsigned i = 0; i <= location->depth; ++i)
        if (system_denied(location->mount, location->nodes[i].id)) return true;
    return false;
}
static bool path_denied(const struct fs_path *path)
{
    return !fs_system_write_allowed() && path->canonical[0] == '/' && path->canonical[1] == 'R' &&
        path->canonical[2] == 'U' && path->canonical[3] == 'M' &&
        (!path->canonical[4] || path->canonical[4] == '/');
}
static void system_refresh(enum fs_mount_id id)
{
    (void)fs_system_index_build(&mounts[id].system, &mounts[id].backend);
}
enum fs_error fs_reference_check_write(fs_reference token)
{
    struct reference *ref = find_reference(token);
    if (!ref) return FS_INVALID;
    struct object *object = &objects[ref->object];
    if (!(ref->access & FS_WRITE)) return FS_ACCESS;
    if ((!fs_system_write_allowed() && object->system) || system_denied(object->mount, object->id)) return FS_ACCESS;
    return FS_OK;
}

static enum fs_error preflight(const struct fs_context *context, const char *path, struct fs_path *plan)
{
    if (context && !context->directory) return FS_INVALID;
    return fs_path_parse(context ? context->path : "/", path, plan);
}

static enum fs_error resolve(const struct fs_context *context, const struct fs_path *plan,
                              unsigned count, struct location *location)
{
    struct object *cwd = NULL;
    if (context) {
        struct reference *ref = find_reference(context->directory);
        if (!ref) return FS_INVALID;
        cwd = &objects[ref->object];
        if (cwd->kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    }
    enum fs_error error = root(location, FS_MOUNT_DISK);
    if (error != FS_OK) return error;
    if (context && !plan->input.absolute) {
        for (unsigned i = 0; i < plan->base.count; ++i) {
            error = step(location, plan->base.text + plan->base.offsets[i]);
            if (error != FS_OK) return error;
        }
        if (cwd->mount != location->mount || cwd->id != location->nodes[location->depth].id)
            return FS_INVALID;
    }
    for (unsigned i = 0; i < count; ++i) {
        error = step(location, plan->input.text + plan->input.offsets[i]);
        if (error != FS_OK) return error;
    }
    if (count == plan->input.count && plan->input.directory &&
        location->nodes[location->depth].kind != FS_DIRECTORY) return FS_NOT_DIRECTORY;
    return FS_OK;
}

static struct fs_information information(const struct location *location)
{
    const struct fs_node *node = &location->nodes[location->depth];
    return (struct fs_information){ .identity = { location->mount, mounts[location->mount].generation, node->id },
                                    .kind = node->kind, .size = node->size,
                                    .read_only_volume = mounts[location->mount].backend.read_only };
}

static bool retained(enum fs_mount_id mount, uint64_t id)
{
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i)
        if (objects[i].references && objects[i].mount == mount && objects[i].id == id) return true;
    return false;
}

static enum fs_error new_reference(const struct location *location, unsigned access, fs_reference *result)
{
    const struct fs_node *node = &location->nodes[location->depth];
    if ((access & FS_WRITE) && location_denied(location)) return FS_ACCESS;
    if (node->kind == FS_DIRECTORY && (access & FS_WRITE)) return FS_IS_DIRECTORY;
    if (mounts[location->mount].backend.read_only && (access & FS_WRITE)) return FS_READ_ONLY;
    if (!next_token) return FS_NO_SPACE;
    unsigned reference_slot = FS_REFERENCE_LIMIT, object_slot = FS_OBJECT_LIMIT, empty = FS_OBJECT_LIMIT;
    for (unsigned i = 0; i < FS_REFERENCE_LIMIT; ++i)
        if (!references[i].token) { reference_slot = i; break; }
    for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i) {
        if (!objects[i].references) { if (empty == FS_OBJECT_LIMIT) empty = i; }
        else if (objects[i].mount == location->mount && objects[i].id == node->id) object_slot = i;
    }
    if (reference_slot == FS_REFERENCE_LIMIT || (object_slot == FS_OBJECT_LIMIT && empty == FS_OBJECT_LIMIT))
        return FS_NO_SPACE;
    bool creating = object_slot == FS_OBJECT_LIMIT;
    if (!creating && (access & FS_WRITE) && objects[object_slot].system && !fs_system_write_allowed()) return FS_ACCESS;
    if (creating) {
        struct fs_backend *backend = &mounts[location->mount].backend;
        enum fs_error error = backend->operations->retain(backend->context, node->id);
        if (error != FS_OK) return error;
        object_slot = empty;
    } else if (objects[object_slot].kind != node->kind) return FS_IO_ERROR;
    uint32_t flags = cpu_interrupt_save();
    if (creating) objects[object_slot] = (struct object){ .id = node->id, .mount = location->mount, .kind = node->kind,
        .system = fs_system_identity(&mounts[location->mount].system, node->id) };
    ++objects[object_slot].references;
    references[reference_slot] = (struct reference){ .token = next_token++, .object = object_slot, .access = access };
    *result = references[reference_slot].token;
    cpu_interrupt_restore(flags);
    return FS_OK;
}

static enum fs_error close_reference(fs_reference token)
{
    struct reference *ref = find_reference(token);
    if (!ref) return FS_INVALID;
    struct object *object = &objects[ref->object];
    struct object saved = *object;
    uint32_t flags = cpu_interrupt_save();
    *ref = (struct reference){0};
    bool last = --object->references == 0;
    if (last) *object = (struct object){0};
    cpu_interrupt_restore(flags);
    if (last) {
        struct fs_backend *backend = &mounts[saved.mount].backend;
        backend->operations->release(backend->context, saved.id);
    }
    return FS_OK;
}

static enum fs_error mount_backend(const struct fs_backend *backend, enum fs_mount_id id)
{
    if (!backend || !backend->root || backend->naming != FS_NAMES_FAT83 || !backend->operations ||
        !backend->operations->lookup || !backend->operations->stat || !backend->operations->retain ||
        !backend->operations->release || !backend->operations->read || !backend->operations->readdir) return FS_INVALID;
    if (!fs_device_control_allowed()) return FS_ACCESS;
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct mount *mount = &mounts[id];
    if (mount->mounted) error = FS_BUSY;
    else if (mount->generation == UINT32_MAX) error = FS_NO_SPACE;
    else {
        struct fs_node node = {0};
        error = backend->operations->stat(backend->context, backend->root, &node);
        if (error == FS_OK) {
            if (!valid_node(&node) || node.id != backend->root || node.kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
            else {
                uint32_t flags = cpu_interrupt_save();
                mount->backend = *backend; ++mount->generation; mount->mounted = true;
                cpu_interrupt_restore(flags);
                system_refresh(id);
            }
        }
    }
    leave(); return error;
}

enum fs_error fs_mount_disk(const struct fs_backend *backend) { return mount_backend(backend, FS_MOUNT_DISK); }

enum fs_error fs_unmount_disk(void)
{
    if (!fs_device_control_allowed()) return FS_ACCESS;
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    if (!mounts[FS_MOUNT_DISK].mounted) error = FS_UNAVAILABLE;
    else {
        for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i)
            if (objects[i].references && objects[i].mount == FS_MOUNT_DISK) error = FS_BUSY;
        if (error == FS_OK) {
            uint32_t flags = cpu_interrupt_save();
            mounts[FS_MOUNT_DISK].mounted = false;
            cpu_interrupt_restore(flags);
            struct fs_backend *backend = &mounts[FS_MOUNT_DISK].backend;
            if (backend->operations->unmount) backend->operations->unmount(backend->context);
        }
    }
    leave(); return error;
}

enum fs_error fs_open(const struct fs_context *context, const char *path, unsigned access, fs_reference *result)
{
    return fs_open_flags(context, path, access, 0, result);
}

enum fs_error fs_open_flags(const struct fs_context *context, const char *path, unsigned access,
                            unsigned flags, fs_reference *result)
{
    if (!result || !access || (access & ~(FS_READ | FS_WRITE)) ||
        (flags & ~(FS_OPEN_CREATE | FS_OPEN_TRUNCATE | FS_OPEN_APPEND | FS_OPEN_EXCLUSIVE)) ||
        ((flags & FS_OPEN_EXCLUSIVE) && !(flags & FS_OPEN_CREATE)) ||
        (flags && !(access & FS_WRITE))) return FS_INVALID;
    *result = 0;
    struct fs_path plan;
    enum fs_error error = preflight(context, path, &plan);
    if (error == FS_OK && (access & FS_WRITE) && path_denied(&plan)) return FS_ACCESS;
    if (error != FS_OK || (error = enter()) != FS_OK) return error;
    struct location location;
    error = resolve(context, &plan, plan.input.count, &location);
    if (error == FS_OK && (access & FS_WRITE) && location_denied(&location)) { leave(); return FS_ACCESS; }
    if (error == FS_OK && (flags & FS_OPEN_EXCLUSIVE)) { leave(); return FS_EXISTS; }
    if (error == FS_NOT_FOUND && (flags & FS_OPEN_CREATE) && plan.input.count && !plan.input.directory) {
        error = resolve(context, &plan, plan.input.count - 1, &location);
        if (error == FS_OK) {
            struct fs_backend *backend = &mounts[location.mount].backend;
            const char *name = plan.input.text + plan.input.offsets[plan.input.count - 1];
            /* Missing parents are never created; reserve reference capacity
               before a mutation that might publish an empty file. */
            unsigned free_refs = 0, free_objects = 0;
            for (unsigned i = 0; i < FS_REFERENCE_LIMIT; ++i) free_refs += !references[i].token;
            for (unsigned i = 0; i < FS_OBJECT_LIMIT; ++i) free_objects += !objects[i].references;
            if (location_denied(&location)) error = FS_ACCESS;
            else if (location.nodes[location.depth].kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
            else if (backend->read_only) error = FS_READ_ONLY;
            else if (!free_refs || !free_objects || !next_token) error = FS_NO_SPACE;
            else if (!backend->operations->write || !backend->operations->replace ||
                     ((flags & FS_OPEN_TRUNCATE) && !backend->operations->truncate)) error = FS_UNSUPPORTED;
            else {
                error = backend->operations->replace(backend->context, location.nodes[location.depth].id, name, NULL, 0);
                if (fs_system_write_allowed()) system_refresh(location.mount);
            }
            if (error == FS_OK) error = step(&location, name);
        }
    }
    if (error == FS_OK && flags) {
        struct fs_node *node = &location.nodes[location.depth];
        struct fs_backend *backend = &mounts[location.mount].backend;
        if (node->kind != FS_FILE) error = FS_IS_DIRECTORY;
        else if (backend->read_only) error = FS_READ_ONLY;
        else if (!backend->operations->write ||
                 ((flags & FS_OPEN_TRUNCATE) && !backend->operations->truncate)) error = FS_UNSUPPORTED;
        else if ((flags & FS_OPEN_TRUNCATE) && retained(location.mount, node->id)) error = FS_BUSY;
    }
    if (error == FS_OK) error = new_reference(&location, access, result);
    if (error == FS_OK && (flags & FS_OPEN_TRUNCATE)) {
        struct fs_backend *backend = &mounts[location.mount].backend;
        error = backend->operations->truncate(backend->context, location.nodes[location.depth].id, 0);
        if (error != FS_OK) { (void)close_reference(*result); *result = 0; }
    }
    leave(); return error;
}

enum fs_error fs_duplicate(fs_reference token, fs_reference *result)
{
    if (!result) return FS_INVALID;
    *result = 0;
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct reference *ref = find_reference(token);
    if (!ref) error = FS_INVALID;
    else {
        struct object *object = &objects[ref->object];
        struct location location = { .mount = object->mount, .nodes = {{ .id = object->id, .kind = object->kind }} };
        error = new_reference(&location, ref->access, result);
    }
    leave(); return error;
}

enum fs_error fs_close(fs_reference token)
{
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    error = close_reference(token); leave(); return error;
}

enum fs_error fs_stat(fs_reference token, struct fs_information *result)
{
    if (!result) return FS_INVALID;
    *result = (struct fs_information){0};
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct reference *ref = find_reference(token);
    if (!ref) error = FS_INVALID;
    else {
        struct object *object = &objects[ref->object];
        struct fs_backend *backend = &mounts[object->mount].backend;
        struct location location = { .mount = object->mount };
        error = backend->operations->stat(backend->context, object->id, &location.nodes[0]);
        if (error == FS_OK && (!valid_node(&location.nodes[0]) || location.nodes[0].id != object->id ||
                              location.nodes[0].kind != object->kind)) error = FS_IO_ERROR;
        if (error == FS_OK) *result = information(&location);
    }
    leave(); return error;
}

enum fs_error fs_stat_path(const struct fs_context *context, const char *path, struct fs_information *result)
{
    if (!result) return FS_INVALID;
    *result = (struct fs_information){0};
    struct fs_path plan;
    enum fs_error error = preflight(context, path, &plan);
    if (error != FS_OK || (error = enter()) != FS_OK) return error;
    struct location location;
    error = resolve(context, &plan, plan.input.count, &location);
    if (error == FS_OK) *result = information(&location);
    leave(); return error;
}

static struct fs_io_result io(fs_reference token, uint64_t offset, void *buffer, size_t bytes, bool write,
                              uint64_t *append_position)
{
    struct fs_io_result result = {0};
    if (bytes && !buffer) { result.error = FS_INVALID; return result; }
    if (bytes > UINT64_MAX - offset) { result.error = FS_RANGE; return result; }
    result.error = enter();
    if (result.error != FS_OK) return result;
    struct reference *ref = find_reference(token);
    if (!ref) result.error = FS_INVALID;
    else {
        struct object *object = &objects[ref->object];
        struct fs_backend *backend = &mounts[object->mount].backend;
        if (!(ref->access & (write ? FS_WRITE : FS_READ))) result.error = FS_ACCESS;
        else if (write && fs_reference_check_write(token) != FS_OK) result.error = FS_ACCESS;
        else if (object->kind != FS_FILE) result.error = FS_IS_DIRECTORY;
        else if (write && backend->read_only) result.error = FS_READ_ONLY;
        else if (bytes) {
            if (append_position) {
                struct fs_node node = {0};
                result.error = backend->operations->stat(backend->context, object->id, &node);
                if (result.error == FS_OK && (node.id != object->id || node.kind != FS_FILE)) result.error = FS_IO_ERROR;
                offset = node.size;
                if (result.error == FS_OK && bytes > UINT64_MAX - offset) result.error = FS_RANGE;
            }
            if (result.error != FS_OK) { leave(); return result; }
            if (write) result = backend->operations->write ?
                backend->operations->write(backend->context, object->id, offset, buffer, bytes) :
                (struct fs_io_result){ .error = FS_UNSUPPORTED };
            else result = backend->operations->read(backend->context, object->id, offset, buffer, bytes);
            if (result.transferred > bytes) result = (struct fs_io_result){ .error = FS_IO_ERROR };
            if (append_position && result.transferred) *append_position = offset + result.transferred;
        }
    }
    leave(); return result;
}

struct fs_io_result fs_read(fs_reference token, uint64_t offset, void *buffer, size_t bytes)
{
    return io(token, offset, buffer, bytes, false, NULL);
}

struct fs_io_result fs_write(fs_reference token, uint64_t offset, const void *buffer, size_t bytes)
{
    return io(token, offset, (void *)buffer, bytes, true, NULL);
}

struct fs_io_result fs_append(fs_reference token, const void *buffer, size_t bytes, uint64_t *position)
{
    if (!position) return (struct fs_io_result){ .error = FS_INVALID };
    return io(token, 0, (void *)buffer, bytes, true, position);
}

enum fs_error fs_readdir(fs_reference token, uint64_t *cursor, struct fs_entry *entry)
{
    if (!cursor || !entry) return FS_INVALID;
    *entry = (struct fs_entry){0};
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct reference *ref = find_reference(token);
    if (!ref) error = FS_INVALID;
    else {
        struct object *object = &objects[ref->object];
        struct fs_backend *backend = &mounts[object->mount].backend;
        if (!(ref->access & FS_READ)) error = FS_ACCESS;
        else if (object->kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
        else {
            uint64_t next = 0;
            error = backend->operations->readdir(backend->context, object->id, *cursor, entry, &next);
            if (error == FS_OK) {
                if (next <= *cursor || fs_name_check(backend->naming, entry->name) != FS_OK ||
                    (entry->kind != FS_FILE && entry->kind != FS_DIRECTORY)) error = FS_IO_ERROR;
                else {
                    if (backend->naming == FS_NAMES_FAT83)
                        for (unsigned i = 0; entry->name[i]; ++i)
                            if (entry->name[i] >= 'a' && entry->name[i] <= 'z') entry->name[i] -= 'a' - 'A';
                    *cursor = next;
                }
            }
        }
    }
    if (error != FS_OK) *entry = (struct fs_entry){0};
    leave(); return error;
}

enum fs_error fs_truncate(fs_reference token, uint64_t size)
{
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct reference *ref = find_reference(token);
    if (!ref) error = FS_INVALID;
    else {
        struct object *object = &objects[ref->object];
        struct fs_backend *backend = &mounts[object->mount].backend;
        if (!(ref->access & FS_WRITE)) error = FS_ACCESS;
        else if (fs_reference_check_write(token) != FS_OK) error = FS_ACCESS;
        else if (object->kind != FS_FILE) error = FS_IS_DIRECTORY;
        else if (backend->read_only) error = FS_READ_ONLY;
        else error = backend->operations->truncate ?
            backend->operations->truncate(backend->context, object->id, size) : FS_UNSUPPORTED;
    }
    leave(); return error;
}

enum fs_error fs_flush(fs_reference token)
{
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct reference *ref = find_reference(token);
    if (!ref) error = FS_INVALID;
    else {
        struct fs_backend *backend = &mounts[objects[ref->object].mount].backend;
        error = backend->operations->flush ? backend->operations->flush(backend->context) :
                backend->read_only ? FS_OK : FS_UNSUPPORTED;
    }
    leave(); return error;
}

enum mutation { REPLACE, REMOVE, MKDIR };
static enum fs_error mutate(const struct fs_context *context, const char *path,
                             const void *data, size_t bytes, enum mutation action)
{
    if (bytes && !data) return FS_INVALID;
    struct fs_path plan;
    enum fs_error error = preflight(context, path, &plan);
    if (error == FS_OK && path_denied(&plan)) return FS_ACCESS;
    if (error != FS_OK || (error = enter()) != FS_OK) return error;
    struct location parent;
    if (!plan.input.count) error = action == REMOVE ? FS_BUSY : FS_IS_DIRECTORY;
    else {
        const char *name = plan.input.text + plan.input.offsets[plan.input.count - 1];
        bool special = equal(name, ".") || equal(name, "..");
        error = resolve(context, &plan, plan.input.count - (special ? 0 : 1), &parent);
        if (error == FS_OK && special) error = action == REMOVE ? FS_BUSY : FS_IS_DIRECTORY;
        if (error == FS_OK && location_denied(&parent)) error = FS_ACCESS;
        if (error == FS_OK && parent.nodes[parent.depth].kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
        if (error == FS_OK) {
            struct fs_backend *backend = &mounts[parent.mount].backend;
            struct fs_node node = {0};
            if (backend->read_only) error = FS_READ_ONLY;
            else {
                error = backend->operations->lookup(backend->context, parent.nodes[parent.depth].id, name, &node);
                bool exists = error == FS_OK;
                if (exists && !valid_node(&node)) error = FS_IO_ERROR;
                else if (exists && system_denied(parent.mount, node.id)) error = FS_ACCESS;
                else if (exists && action == MKDIR) error = FS_EXISTS;
                else if (exists && plan.input.directory && node.kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
                else if (exists && action == REPLACE && node.kind == FS_DIRECTORY) error = FS_IS_DIRECTORY;
                else if (exists && retained(parent.mount, node.id)) error = FS_BUSY;
                else if (!exists && error == FS_NOT_FOUND && action != REMOVE) {
                    if (plan.input.directory && action != MKDIR) error = FS_NOT_FOUND;
                    else error = FS_OK;
                }
                if (error == FS_OK) {
                    uint64_t id = parent.nodes[parent.depth].id;
                    if (action == REPLACE) error = backend->operations->replace ?
                        backend->operations->replace(backend->context, id, name, data, bytes) : FS_UNSUPPORTED;
                    else if (action == REMOVE) error = backend->operations->remove ?
                        backend->operations->remove(backend->context, id, name) : FS_UNSUPPORTED;
                    else error = backend->operations->mkdir ?
                        backend->operations->mkdir(backend->context, id, name) : FS_UNSUPPORTED;
                    if (fs_system_write_allowed()) system_refresh(parent.mount);
                }
            }
        }
    }
    leave(); return error;
}

enum fs_error fs_replace(const struct fs_context *context, const char *path, const void *data, size_t bytes)
{
    return mutate(context, path, data, bytes, REPLACE);
}
enum fs_error fs_remove(const struct fs_context *context, const char *path) { return mutate(context, path, NULL, 0, REMOVE); }
enum fs_error fs_mkdir(const struct fs_context *context, const char *path) { return mutate(context, path, NULL, 0, MKDIR); }

enum fs_error fs_rename(const struct fs_context *context, const char *source, const char *destination, bool replace)
{
    /* Parse both complete paths before the first backend lookup. Keep original
       traversal semantics: missing/../name is still missing. */
    struct fs_path from, to;
    enum fs_error error = preflight(context, source, &from);
    if (error == FS_OK) error = preflight(context, destination, &to);
    if (error != FS_OK) return error;
    if (path_denied(&from) || path_denied(&to)) return FS_ACCESS;
    if (!from.input.count || !to.input.count || from.input.directory || to.input.directory) return FS_INVALID;
    const char *old = from.input.text + from.input.offsets[from.input.count - 1];
    const char *name = to.input.text + to.input.offsets[to.input.count - 1];
    if (equal(old, ".") || equal(old, "..") || equal(name, ".") || equal(name, "..")) return FS_INVALID;
    error = enter();
    if (error != FS_OK) return error;
    struct location parent, target;
    error = resolve(context, &from, from.input.count - 1, &parent);
    if (error == FS_OK) error = resolve(context, &to, to.input.count - 1, &target);
    if (error == FS_OK && (location_denied(&parent) || location_denied(&target))) error = FS_ACCESS;
    if (error == FS_OK && parent.mount != target.mount) error = FS_CROSS_DEVICE;
    if (error == FS_OK && (parent.nodes[parent.depth].kind != FS_DIRECTORY || target.nodes[target.depth].kind != FS_DIRECTORY))
        error = FS_NOT_DIRECTORY;
    if (error == FS_OK) {
        struct fs_backend *backend = &mounts[parent.mount].backend;
        struct fs_node node = {0}, existing = {0};
        if (backend->read_only) error = FS_READ_ONLY;
        else error = backend->operations->lookup(backend->context, parent.nodes[parent.depth].id, old, &node);
        if (error == FS_OK && !valid_node(&node)) error = FS_IO_ERROR;
        if (error == FS_OK && system_denied(parent.mount, node.id)) error = FS_ACCESS;
        if (error == FS_OK && node.kind != FS_FILE) error = FS_IS_DIRECTORY;
        if (error == FS_OK && retained(parent.mount, node.id)) error = FS_BUSY;
        if (error == FS_OK) {
            error = backend->operations->lookup(backend->context, target.nodes[target.depth].id, name, &existing);
            if (error == FS_OK) {
                if (!valid_node(&existing)) error = FS_IO_ERROR;
                else if (system_denied(target.mount, existing.id)) error = FS_ACCESS;
                else if (existing.id == node.id) error = FS_INVALID;
                else if (!replace) error = FS_EXISTS;
                else if (existing.kind != FS_FILE) error = FS_IS_DIRECTORY;
                else if (retained(parent.mount, existing.id)) error = FS_BUSY;
            } else if (error == FS_NOT_FOUND) error = FS_OK;
        }
        if (error == FS_OK) {
            error = backend->operations->rename ? backend->operations->rename(backend->context,
                parent.nodes[parent.depth].id, old, target.nodes[target.depth].id, name, replace) : FS_UNSUPPORTED;
            if (fs_system_write_allowed()) system_refresh(parent.mount);
        }
    }
    leave(); return error;
}

enum fs_error fs_context_initialize(struct fs_context *context)
{
    if (!context || context->directory) return FS_INVALID;
    fs_reference directory;
    enum fs_error error = fs_open(NULL, "/", FS_READ, &directory);
    if (error == FS_OK) *context = (struct fs_context){ .directory = directory, .path = "/" };
    return error;
}

enum fs_error fs_context_clone(const struct fs_context *source, struct fs_context *destination)
{
    if (!source || !destination || destination->directory) return FS_INVALID;
    /* Kernel tasks can exist before a usable disk. They carry no filesystem
       reference; acquire the root when a later child starts with a disk. */
    if (!source->directory) {
        enum fs_error error = fs_context_initialize(destination);
        return error == FS_UNAVAILABLE ? FS_OK : error;
    }
    enum fs_error error = enter();
    if (error != FS_OK) return error;
    struct reference *ref = find_reference(source->directory);
    if (!ref) error = FS_INVALID;
    else {
        struct object *object = &objects[ref->object];
        if (object->kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
        else {
            struct location location = { .mount = object->mount, .nodes = {{ .id = object->id, .kind = FS_DIRECTORY }} };
            fs_reference token;
            error = new_reference(&location, FS_READ, &token);
            if (error == FS_OK) { *destination = *source; destination->directory = token; }
        }
    }
    leave(); return error;
}

enum fs_error fs_context_chdir(struct fs_context *context, const char *path)
{
    if (!context) return FS_INVALID;
    struct fs_path plan;
    enum fs_error error = preflight(context, path, &plan);
    if (error != FS_OK || (error = enter()) != FS_OK) return error;
    struct location location;
    error = resolve(context, &plan, plan.input.count, &location);
    if (error == FS_OK && location.nodes[location.depth].kind != FS_DIRECTORY) error = FS_NOT_DIRECTORY;
    if (error == FS_OK) {
        fs_reference directory;
        error = new_reference(&location, FS_READ, &directory);
        if (error == FS_OK) {
            fs_reference old = context->directory;
            uint32_t flags = cpu_interrupt_save();
            context->directory = directory;
            memcpy(context->path, plan.canonical, FS_PATH_CAPACITY);
            cpu_interrupt_restore(flags);
            (void)close_reference(old);
        }
    }
    leave(); return error;
}

enum fs_error fs_context_destroy(struct fs_context *context)
{
    if (!context) return FS_INVALID;
    if (!context->directory) { *context = (struct fs_context){0}; return FS_OK; }
    enum fs_error error = fs_close(context->directory);
    if (error == FS_OK) *context = (struct fs_context){0};
    return error;
}
