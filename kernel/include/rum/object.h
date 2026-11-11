#ifndef RUM_OBJECT_H
#define RUM_OBJECT_H

#include <rum/abi/types.h>

/* Kernel-only ownership foundation. No header, pointer or callback is ABI. */
#define OBJECT_LIMIT 1024u
#define OBJECT_REFERENCE_LIMIT 65535u
enum object_kind {
    OBJECT_INVALID, OBJECT_FILE, OBJECT_DIRECTORY, OBJECT_PIPE_READER,
    OBJECT_PIPE_WRITER, OBJECT_CONSOLE_INPUT, OBJECT_CONSOLE_OUTPUT,
    OBJECT_PROCESS, OBJECT_KIND_COUNT
};
enum object_reference { OBJECT_OWNER, OBJECT_TABLE, OBJECT_OPERATION, OBJECT_REFERENCE_COUNT };
enum object_state { OBJECT_EMPTY, OBJECT_CONSTRUCTING, OBJECT_PUBLISHED, OBJECT_DESTROYING };
struct kernel_object;
struct object_type {
    enum object_kind kind;
    /* Task context, IF set, outside known scheduling/FS/block guards. Zero
       consumes the object and may free its storage; the core never reads it
       again. On error keep the header and last reference intact for retry.
       Unpublished construction also uses this callback. Type storage is static.
       Do not recycle this header until the callback has returned. */
    rum_result_t (*destroy)(struct kernel_object *);
    /* Optional current-context ceiling, checked for operations and new grants.
       Pure, bounded and allocation-free; may run with IF clear. It must not
       acquire references, touch user memory or perform filesystem/backend I/O. */
    rum_result_t (*authorize)(struct kernel_object *, uint32_t rights);
};
struct kernel_object {
    const struct object_type *type;
    enum object_state state;
    uint32_t references;
    uint32_t owners[OBJECT_REFERENCE_COUNT];
};
struct object_counts {
    uint32_t objects, constructing, published, destroying;
    uint32_t references[OBJECT_REFERENCE_COUNT];
};
struct object_statistics {
    struct object_counts total;
    struct object_counts kinds[OBJECT_KIND_COUNT];
};

/* Initialize fresh zeroed storage in a destruction-safe task context, owning
   one construction reference. Finish all payload fields before publication.
   Publish transfers that one reference to its first owner without incrementing
   the count. Caller atomically installs the pointer with IF clear if needed. */
rum_result_t object_initialize(struct kernel_object *, const struct object_type *);
/* Correct a reserved constructor's type before publication (for example,
   resolving a path to a file versus a directory). No reference may escape. */
rum_result_t object_retype(struct kernel_object *, const struct object_type *);
rum_result_t object_publish(struct kernel_object *, enum object_reference);
/* Caller already owns/borrows a retained reference, never an arbitrary cached
   pointer. Published objects only. Transfer reclassifies an owned reference;
   acquire adds another. These operations do not allocate, block or call out. */
rum_result_t object_acquire(struct kernel_object *, enum object_reference);
rum_result_t object_transfer(struct kernel_object *, enum object_reference from, enum object_reference to);
/* Failure preserves ownership. Nonfinal release may run with IF clear; final
   release must pass task_blocking_allowed(). IRQ mutations are always refused.
   During destruction no reference may be acquired, transferred or released. */
rum_result_t object_release(struct kernel_object *, enum object_reference);
/* Bounded, allocation-free and IRQ-safe; includes constructing and destroying
   objects until their successful final release. Counts only, never pointers. */
struct object_statistics object_stats(void);

#endif
