#ifndef RUM_ABI_FILESYSTEM_H
#define RUM_ABI_FILESYSTEM_H

#include <rum/abi/types.h>

/* Capacities include NUL. Depth counts names below a mount root. */
#define RUM_ABI_PATH_CAPACITY      256
#define RUM_ABI_COMPONENT_CAPACITY 64
#define RUM_ABI_PATH_DEPTH         16

#define RUM_FS_ABI_VERSION 1
#define RUM_ABI_HANDLE_LIMIT 32
#define RUM_OPEN_READ 1
#define RUM_OPEN_WRITE 2
#define RUM_OPEN_DIRECTORY 1 /* Only directories; read access is required. */
#define RUM_SEEK_SET 0
#define RUM_SEEK_CUR 1
#define RUM_SEEK_END 2
#define RUM_ENTRY_FILE 1
#define RUM_ENTRY_DIRECTORY 2
#define RUM_ABI_REPLACE_LIMIT 65536

#ifndef __ASSEMBLER__
#include <stddef.h>
/* Every word has the same layout on i386 and a host. Reserved words must be
   zero. Inputs require the exact size and version; extensions use a new version.
   Paths are NUL-terminated user strings, bounded by PATH_CAPACITY. */
struct rum_open_request {
    uint32_t version, size;
    rum_address_t path;
    uint32_t access, flags, reserved;
};
/* Signed two's-complement 64-bit displacement in offset_hi:offset_lo.
   position_hi:position_lo is output only and must initially be zero. */
struct rum_seek_request {
    uint32_t version, size, offset_lo, offset_hi, whence, reserved;
    uint32_t position_lo, position_hi;
};
/* Initialize version/size and zero reserved. Other fields are output only;
   the same packet can be reused. readdir returns 1 for an entry, 0 for end,
   or a negative error. A failed call does not advance. */
struct rum_directory_entry {
    uint32_t version, size, kind, size_lo, size_hi, reserved;
    char name[RUM_ABI_COMPONENT_CAPACITY];
};
/* Whole-file create/replace. A failure may follow committed disk metadata;
   follow the backend's interrupted-write guarantee. No open identity may be
   replaced. Zero bytes ignores data; nonempty buffers are checked completely. */
struct rum_replace_request {
    uint32_t version, size;
    rum_address_t path, data;
    rum_size_t bytes;
    uint32_t reserved;
};
_Static_assert(sizeof(struct rum_replace_request) == 24, "replace packet layout");
_Static_assert(sizeof(struct rum_open_request) == 24 &&
               sizeof(struct rum_seek_request) == 32 &&
               sizeof(struct rum_directory_entry) == 88 &&
               offsetof(struct rum_directory_entry, name) == 24, "filesystem ABI layout");
#endif

#endif
