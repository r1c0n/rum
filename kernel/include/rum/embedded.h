#ifndef RUM_EMBEDDED_H
#define RUM_EMBEDDED_H
#include <stddef.h>

struct embedded_file { const char *name; const unsigned char *data; size_t size; };
extern const struct embedded_file embedded_files[];
extern const size_t embedded_file_count;
#endif
