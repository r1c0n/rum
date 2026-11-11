#ifndef RUM_HANDLES_H
#define RUM_HANDLES_H
#include <rum/abi/filesystem.h>
#include <rum/abi/process.h>
#include <rum/fs.h>
#include <rum/object_handle.h>

enum process_handle_kind { HANDLE_UNUSED, HANDLE_INPUT, HANDLE_OUTPUT, HANDLE_FILE, HANDLE_DIRECTORY,
                           HANDLE_PIPE_READ, HANDLE_PIPE_WRITE };
struct kernel_pipe;
/* A bounded, shared open description. Callers retain ownership through a
   descriptor; do not cache a pointer after closing that descriptor. */
struct process_handle {
    struct kernel_object object;
    fs_reference reference;
    uint64_t offset;
    unsigned access;
    bool append;
    enum process_handle_kind kind;
    rum_result_t pending_error;
    uint32_t descriptors;
    struct kernel_pipe *pipe;
};
/* Entries own references to open descriptions; flags belong to each entry. */
struct process_descriptor { struct process_handle *object; uint32_t flags; bool closing; };
struct process_handles { struct process_descriptor slots[RUM_ABI_HANDLE_LIMIT]; };
struct process_handle_statistics { uint32_t descriptions, descriptors, aliases; };

/* Initialize a new private table. Prepare-spawn also requires a private empty
   destination; on failure it owns no acquired references. Task context only. */
rum_result_t process_handles_initialize(struct process_handles *);
struct process_handle_statistics process_handles_stats(void);
rum_result_t process_handle_duplicate(struct process_handles *, rum_handle_t source, rum_handle_t target, uint32_t flags);
rum_result_t process_handles_prepare_spawn(struct process_handles *, const struct process_handles *,
                                         const struct rum_handle_mapping *, unsigned count, unsigned flags);
unsigned process_handles_count(const struct process_handles *);
rum_result_t process_handles_destroy(struct process_handles *);
struct process_handle *process_handle_get(struct process_handles *, rum_handle_t);
/* A borrowed get is valid only while its descriptor remains owned. Acquisition
   returns an independent OPERATION reference, including across blocked I/O. */
rum_result_t process_handle_acquire(struct process_handles *, rum_handle_t, struct process_handle **);
struct process_handle *process_handle_from_object(struct kernel_object *);
uint32_t process_handle_rights(const struct process_handle *);
rum_result_t process_handle_export(struct process_handles *, rum_handle_t, struct object_handles *,
                                   uint32_t type, uint32_t rights, uint32_t flags, rum_object_handle_t *);
rum_result_t process_handle_open(struct process_handles *, const struct fs_context *, const char *, unsigned, unsigned);
rum_result_t process_handle_close(struct process_handles *, rum_handle_t);
rum_result_t process_handles_pipe(struct process_handles *, rum_handle_t *reader, rum_handle_t *writer);
rum_result_t process_handle_seek(struct process_handle *, uint64_t displacement, unsigned whence, uint64_t *position);
rum_result_t process_handle_read(struct process_handle *, void *, size_t);
rum_result_t process_handle_write(struct process_handle *, const void *, size_t);
rum_result_t process_handle_readdir(struct process_handle *, struct fs_entry *);
rum_result_t process_handle_flush(struct process_handle *);
rum_result_t process_fs_error(enum fs_error);
/* Guard synchronous filesystem operations, including cleanup, at boundaries. */
bool process_filesystem_context(void);
#endif
