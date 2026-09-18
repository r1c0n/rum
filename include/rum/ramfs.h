#ifndef RUM_RAMFS_H
#define RUM_RAMFS_H

#include <stdbool.h>
#include <stddef.h>

#define RAMFS_NAME_CAPACITY 64u
#define RAMFS_FILE_LIMIT (64u * 1024u)
#define RAMFS_MAX_FILES 64u

struct ramfs_statistics { size_t files, bytes; };
typedef bool (*ramfs_visitor)(const char *name, size_t size, void *context);

/* Foreground only. One flat root; names use ASCII letters/digits/._-, with an
   optional leading '/'. '.' and '..' are rejected. No disk persistence. */
bool ramfs_initialize(void);
bool ramfs_put(const char *name, const void *data, size_t size);
/* Borrowed bytes remain valid until this file is replaced or removed. */
bool ramfs_read(const char *name, const unsigned char **data, size_t *size);
bool ramfs_remove(const char *name);
/* Return false from visitor to stop. Visitors must not mutate the filesystem. */
void ramfs_list(ramfs_visitor visitor, void *context);
struct ramfs_statistics ramfs_stats(void);
bool embedded_files_install(void); /* Build-generated boot files; install once. */

#endif
