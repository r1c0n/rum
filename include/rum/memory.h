#ifndef RUM_MEMORY_H
#define RUM_MEMORY_H

#include <stddef.h>

/* GCC can generate calls to these even when compiling freestanding code. */
void *memcpy(void *restrict destination, const void *restrict source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);

#endif
