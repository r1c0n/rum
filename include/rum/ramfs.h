#ifndef RUM_RAMFS_H
#define RUM_RAMFS_H

#include <stdbool.h>
#include <stddef.h>
#include <rum/fs.h>

#define RAMFS_NAME_CAPACITY 64u
#define RAMFS_FILE_LIMIT (64u * 1024u)
#define RAMFS_MAX_FILES 64u
_Static_assert(RAMFS_NAME_CAPACITY == FS_NAME_CAPACITY, "RAM filename limit must match common parser");

struct ramfs_statistics { size_t files, bytes; };
typedef bool (*ramfs_visitor)(const char *name, size_t size, void *context);

/* Foreground only. One flat root; names use ASCII letters/digits/._-, with an
   optional leading '/'. '.' and '..' are rejected. No disk persistence. */
bool ramfs_initialize(void);
bool ramfs_put(const char *name, const void *data, size_t size);
/* Borrowed bytes remain valid until this file is written, replaced or removed.
   Whole-file replacement/removal fail while a common-FS reference is open. */
bool ramfs_read(const char *name, const unsigned char **data, size_t *size);
bool ramfs_remove(const char *name);
/* Return false from visitor to stop. Visitors must not mutate the filesystem. */
void ramfs_list(ramfs_visitor visitor, void *context);
struct ramfs_statistics ramfs_stats(void);
bool embedded_files_install(void); /* Build-generated boot files; install once. */
/* Namespace owner only: reserve a mounted disk without hiding a RAM file. */
void ramfs_reserve_disk(bool reserved);
void ramfs_reserve_system(bool reserved);

#endif
