#ifndef RUM_C_STDLIB_H
#define RUM_C_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
_Noreturn void exit(int);
_Noreturn void abort(void);
#endif
