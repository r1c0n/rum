#ifndef RUM_HEAP_H
#define RUM_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rum/memory_layout.h>

#define HEAP_BASE RUM_HEAP_BASE
#define HEAP_LIMIT (RUM_HEAP_END - RUM_HEAP_BASE)
#define HEAP_ALIGNMENT 16u

struct heap_statistics {
    size_t mapped_bytes;
    size_t used_bytes; /* Aligned live payload capacity, excluding headers. */
    size_t free_bytes;
    size_t allocations;
};

/* Initialize after paging. Foreground only; never allocate from an IRQ handler.
   Pages remain mapped for reuse. Zero-size allocation returns NULL. */
bool heap_initialize(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *pointer, size_t size);
bool kfree(void *pointer); /* NULL succeeds; invalid/interior/double frees fail. */
struct heap_statistics heap_stats(void);

#endif
