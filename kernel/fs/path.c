#include <rum/memory.h>
#include <rum/path.h>

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static bool valid_byte(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

enum fs_error fs_name_check(enum fs_naming naming, const char *name)
{
    if (!name || !*name || equal(name, ".") || equal(name, "..") ||
        (naming != FS_NAMES_RAM && naming != FS_NAMES_FAT83)) return FS_INVALID;
    unsigned base = 0, extension = 0;
    bool dot = false;
    for (unsigned i = 0; name[i]; ++i) {
        if (i >= FS_NAME_CAPACITY - 1) return FS_NAME_TOO_LONG;
        if (!valid_byte((unsigned char)name[i])) return FS_INVALID;
        if (naming != FS_NAMES_FAT83) continue;
        if (name[i] == '.') {
            if (dot || !base) return FS_INVALID;
            dot = true;
        } else if (dot) {
            if (++extension > 3) return FS_NAME_TOO_LONG;
        } else if (++base > 8) return FS_NAME_TOO_LONG;
    }
    return dot && !extension ? FS_INVALID : FS_OK;
}

static enum fs_error collect(const char *input, struct fs_tokens *tokens)
{
    if (!input || !*input) return FS_INVALID;
    unsigned length = 0;
    while (length < FS_PATH_CAPACITY && input[length]) {
        if (input[length] != '/' && !valid_byte((unsigned char)input[length])) return FS_INVALID;
        ++length;
    }
    if (length == FS_PATH_CAPACITY) return FS_PATH_TOO_LONG;
    memcpy(tokens->text, input, length + 1);
    tokens->absolute = input[0] == '/';
    tokens->directory = input[length - 1] == '/';
    unsigned i = 0;
    while (i < length) {
        while (i < length && tokens->text[i] == '/') tokens->text[i++] = '\0';
        if (i == length) break;
        if (tokens->count == FS_TOKEN_LIMIT) return FS_TOO_DEEP;
        unsigned start = i;
        while (i < length && tokens->text[i] != '/') ++i;
        if (i - start >= FS_NAME_CAPACITY) return FS_NAME_TOO_LONG;
        tokens->offsets[tokens->count++] = (uint16_t)start;
        if (i < length) tokens->text[i++] = '\0';
    }
    if (tokens->count) {
        const char *last = tokens->text + tokens->offsets[tokens->count - 1];
        tokens->directory |= equal(last, ".") || equal(last, "..");
    }
    return FS_OK;
}

struct canonical {
    char *text;
    unsigned length, count, previous[FS_PATH_DEPTH + 1];
    enum fs_mount_id mount;
};

static enum fs_error apply(struct canonical *path, struct fs_tokens *tokens)
{
    for (unsigned i = 0; i < tokens->count; ++i) {
        char *name = tokens->text + tokens->offsets[i];
        if (equal(name, ".")) continue;
        if (equal(name, "..")) {
            if (path->mount == FS_MOUNT_DISK && path->count == 1) return FS_MOUNT_ESCAPE;
            if (path->count) path->length = path->previous[--path->count];
            path->text[path->length] = '\0';
            continue;
        }
        bool disk_root = path->mount == FS_MOUNT_RAM && !path->count && equal(name, "disk");
        enum fs_error error = fs_name_check(path->mount == FS_MOUNT_DISK ? FS_NAMES_FAT83 : FS_NAMES_RAM, name);
        if (error != FS_OK) return error;
        unsigned depth = path->count - (path->mount == FS_MOUNT_DISK ? 1 : 0);
        if (!disk_root && depth == FS_PATH_DEPTH) return FS_TOO_DEEP;
        unsigned length = 0;
        while (name[length]) {
            if (path->mount == FS_MOUNT_DISK && name[length] >= 'a' && name[length] <= 'z')
                name[length] = (char)(name[length] - 'a' + 'A');
            ++length;
        }
        unsigned slash = path->length > 1;
        if (length >= FS_PATH_CAPACITY - path->length - slash) return FS_PATH_TOO_LONG;
        path->previous[path->count++] = path->length;
        if (slash) path->text[path->length++] = '/';
        memcpy(path->text + path->length, name, length + 1);
        path->length += length;
        if (disk_root) path->mount = FS_MOUNT_DISK;
    }
    return FS_OK;
}

enum fs_error fs_path_parse(const char *base, const char *input, struct fs_path *result)
{
    if (!result) return FS_INVALID;
    *result = (struct fs_path){0};
    enum fs_error error = collect(base, &result->base);
    if (error != FS_OK) return error;
    if (!result->base.absolute) return FS_INVALID;
    error = collect(input, &result->input);
    if (error != FS_OK) return error;
    struct canonical path = { .text = result->canonical, .length = 1, .mount = FS_MOUNT_RAM };
    result->canonical[0] = '/';
    error = apply(&path, &result->base);
    if (error != FS_OK) return error;
    if (result->input.absolute) {
        path = (struct canonical){ .text = result->canonical, .length = 1, .mount = FS_MOUNT_RAM };
        result->canonical[1] = '\0';
    }
    error = apply(&path, &result->input);
    if (error != FS_OK) return error;
    result->mount = path.mount;
    result->depth = (uint16_t)(path.count - (path.mount == FS_MOUNT_DISK ? 1 : 0));
    return FS_OK;
}
