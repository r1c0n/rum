#ifndef RUM_HANDLES_H
#define RUM_HANDLES_H
#include <rum/abi/filesystem.h>
#include <rum/fs.h>

enum process_handle_kind { HANDLE_UNUSED, HANDLE_INPUT, HANDLE_OUTPUT, HANDLE_FILE, HANDLE_DIRECTORY };
struct process_handle {
    fs_reference reference;
    uint64_t offset;
    unsigned access;
    enum process_handle_kind kind;
    rum_result_t pending_error;
};
struct process_handles { struct process_handle slots[RUM_ABI_HANDLE_LIMIT]; };

void process_handles_initialize(struct process_handles *);
unsigned process_handles_count(const struct process_handles *);
rum_result_t process_handles_destroy(struct process_handles *);
struct process_handle *process_handle_get(struct process_handles *, rum_handle_t);
rum_result_t process_handle_open(struct process_handles *, const struct fs_context *, const char *, unsigned, unsigned);
rum_result_t process_handle_close(struct process_handles *, rum_handle_t);
rum_result_t process_handle_seek(struct process_handle *, uint64_t displacement, unsigned whence, uint64_t *position);
rum_result_t process_handle_read(struct process_handle *, void *, size_t);
rum_result_t process_handle_write(struct process_handle *, const void *, size_t);
rum_result_t process_handle_readdir(struct process_handle *, struct fs_entry *);
rum_result_t process_handle_flush(struct process_handle *);
rum_result_t process_fs_error(enum fs_error);
/* Guard synchronous filesystem operations, including cleanup, at boundaries. */
bool process_filesystem_context(void);
#endif
