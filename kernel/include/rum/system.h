#ifndef RUM_SYSTEM_H
#define RUM_SYSTEM_H
#include <rum/fs.h>
#include <rum/multiboot.h>

#define SYSTEM_IMAGE_LIMIT (1024u * 1024u)
#define SYSTEM_FILE_LIMIT 64u
#define SYSTEM_FILE_BYTES 65536u

/* Little-endian, fixed-width archive shared with scripts/pack-system.py. */
struct system_header {
    char magic[8];
    uint32_t version, bytes, files, entry_bytes, header_bytes, reserved;
};
struct system_entry {
    char name[64];
    uint32_t offset, bytes, reserved[2];
};
_Static_assert(sizeof(struct system_header) == 32 && sizeof(struct system_entry) == 80,
               "system image layout");

/* Boot only, after PMM and before paging: copy the first Multiboot module
   into owned low physical pages. Malformed/missing images fail closed. */
enum fs_error system_files_prepare(const struct multiboot_info *);
enum fs_error system_files_mount(void);
#endif
