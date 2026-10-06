#ifndef RUM_FS_H
#define RUM_FS_H
#include <rum/fs_types.h>
#include <rum/fs_limits.h>

struct fs_entry { char name[FS_NAME_CAPACITY]; enum fs_kind kind; uint64_t size; };
/* Backend IDs are nonzero and stable while retained. Lookup/stat/readdir do
   not acquire references. retain/release must never block or perform disk I/O.
   Mutating callers, including any backend-specific API, must honor retention.
   readdir returns FS_END without an entry at EOF; cursors are opaque. */
struct fs_operations {
    enum fs_error (*lookup)(void *, uint64_t parent, const char *, struct fs_node *);
    enum fs_error (*stat)(void *, uint64_t id, struct fs_node *);
    enum fs_error (*retain)(void *, uint64_t id);
    void (*release)(void *, uint64_t id);
    struct fs_io_result (*read)(void *, uint64_t id, uint64_t offset, void *, size_t);
    struct fs_io_result (*write)(void *, uint64_t id, uint64_t offset, const void *, size_t);
    enum fs_error (*replace)(void *, uint64_t parent, const char *, const void *, size_t);
    enum fs_error (*remove)(void *, uint64_t parent, const char *);
    enum fs_error (*mkdir)(void *, uint64_t parent, const char *);
    enum fs_error (*readdir)(void *, uint64_t id, uint64_t cursor, struct fs_entry *, uint64_t *next);
    enum fs_error (*flush)(void *);
};
struct fs_backend {
    const struct fs_operations *operations;
    void *context;
    uint64_t root;
    enum fs_naming naming;
    bool read_only;
};
struct fs_identity { enum fs_mount_id mount; uint32_t generation; uint64_t object; };
struct fs_information { struct fs_identity identity; enum fs_kind kind; uint64_t size; };
typedef uint64_t fs_reference; /* Kernel token, not a public process handle. */
struct fs_context { fs_reference directory; char path[FS_PATH_CAPACITY]; };
struct fs_statistics { uint32_t objects, references, mounts; };

/* Idempotent, allocation-free setup of RAM '/' and a reserved '/disk' slot.
   Foreground/boot only; no IRQ callers. NULL path context means '/'. */
bool fs_initialize(void);
enum fs_error fs_mount_disk(const struct fs_backend *backend);
enum fs_error fs_unmount_disk(void);
struct fs_statistics fs_stats(void);
enum fs_error fs_open(const struct fs_context *, const char *path, unsigned access, fs_reference *);
enum fs_error fs_duplicate(fs_reference, fs_reference *);
enum fs_error fs_close(fs_reference);
enum fs_error fs_stat(fs_reference, struct fs_information *);
enum fs_error fs_stat_path(const struct fs_context *, const char *, struct fs_information *);
struct fs_io_result fs_read(fs_reference, uint64_t offset, void *, size_t);
struct fs_io_result fs_write(fs_reference, uint64_t offset, const void *, size_t);
enum fs_error fs_readdir(fs_reference, uint64_t *cursor, struct fs_entry *);
enum fs_error fs_flush(fs_reference);
enum fs_error fs_replace(const struct fs_context *, const char *, const void *, size_t);
enum fs_error fs_remove(const struct fs_context *, const char *);
enum fs_error fs_mkdir(const struct fs_context *, const char *);
/* Initialize/clone into a zeroed context. Changes acquire a new directory
   reference before releasing the old one. Destroy once at owner teardown. */
enum fs_error fs_context_initialize(struct fs_context *);
enum fs_error fs_context_clone(const struct fs_context *, struct fs_context *);
enum fs_error fs_context_chdir(struct fs_context *, const char *);
enum fs_error fs_context_destroy(struct fs_context *);
/* RAM adapter is owned by ramfs; providers own mounted backend storage. */
const struct fs_backend *ramfs_backend(void);
#endif
