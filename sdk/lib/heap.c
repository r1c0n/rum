#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <rum/user.h>

#define ALIGNMENT 16u
#define ARENA_BYTES 65536u
struct block {
    size_t bytes, requested;
    struct block *previous, *next;
};
_Static_assert(sizeof(struct block) % ALIGNMENT == 0, "aligned heap payloads");
struct arena { struct block *first; uint32_t bytes; };
static struct arena arenas[RUM_VM_REGION_LIMIT];

static int allocation_size(size_t bytes, size_t *rounded)
{
    if (bytes > UINT32_MAX - sizeof(struct block) - RUM_ABI_PAGE_SIZE ||
        bytes > SIZE_MAX - (ALIGNMENT - 1)) {
        errno = ENOMEM; return 0;
    }
    *rounded = (bytes + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1);
    return 1;
}
static void join_next(struct block *block)
{
    struct block *next = block->next;
    block->bytes += sizeof *next + next->bytes;
    block->next = next->next;
    if (block->next) block->next->previous = block;
}
static void split(struct block *block, size_t bytes)
{
    if (block->bytes - bytes < sizeof *block + ALIGNMENT) return;
    struct block *tail = (void *)((unsigned char *)(block + 1) + bytes);
    *tail = (struct block){ .bytes = block->bytes - bytes - sizeof *tail,
        .previous = block, .next = block->next };
    if (tail->next) tail->next->previous = tail;
    block->next = tail; block->bytes = bytes;
    if (tail->next && !tail->next->requested) join_next(tail);
}
static struct block *grow(size_t bytes)
{
    unsigned index;
    for (index = 0; index < RUM_VM_REGION_LIMIT; ++index) if (!arenas[index].first) break;
    if (index == RUM_VM_REGION_LIMIT) { errno = ENOMEM; return NULL; }
    uint32_t minimum = (uint32_t)(bytes + sizeof(struct block) + RUM_ABI_PAGE_SIZE - 1) &
                       ~(uint32_t)(RUM_ABI_PAGE_SIZE - 1);
    struct rum_vm_allocate_request request = { .version = RUM_VM_ABI_VERSION,
        .size = sizeof request, .bytes = minimum < ARENA_BYTES ? ARENA_BYTES : minimum,
        .protection = RUM_VM_READ | RUM_VM_WRITE };
    rum_result_t result = rum_vm_allocate(&request);
    /* A small request can still succeed near the page budget or on low RAM. */
    if (result < 0 && request.bytes != minimum) {
        request.bytes = minimum; result = rum_vm_allocate(&request);
    }
    if (result < 0) { errno = (int)-result; return NULL; }
    struct block *first = (void *)(uintptr_t)request.address;
    *first = (struct block){ .bytes = request.bytes - sizeof *first };
    arenas[index] = (struct arena){ first, request.bytes };
    return first;
}
void *malloc(size_t bytes)
{
    if (!bytes) return NULL;
    size_t rounded;
    if (!allocation_size(bytes, &rounded)) return NULL;
    struct block *block = NULL;
    for (unsigned i = 0; i < RUM_VM_REGION_LIMIT && !block; ++i)
        for (struct block *candidate = arenas[i].first; candidate; candidate = candidate->next)
            if (!candidate->requested && candidate->bytes >= rounded) { block = candidate; break; }
    if (!block && !(block = grow(rounded))) return NULL;
    split(block, rounded); block->requested = bytes;
    return block + 1;
}
void free(void *pointer)
{
    if (!pointer) return;
    int saved = errno;
    struct block *block = (struct block *)pointer - 1;
    block->requested = 0;
    if (block->next && !block->next->requested) join_next(block);
    if (block->previous && !block->previous->requested) { block = block->previous; join_next(block); }
    if (!block->previous && !block->next)
        for (unsigned i = 0; i < RUM_VM_REGION_LIMIT; ++i)
            if (arenas[i].first == block) {
                /* Keep a free, reusable arena if release is temporarily denied. */
                if (rum_vm_release((rum_address_t)(uintptr_t)block, arenas[i].bytes) == 0)
                    arenas[i] = (struct arena){0};
                break;
            }
    errno = saved;
}
void *calloc(size_t count, size_t bytes)
{
    if (bytes && count > SIZE_MAX / bytes) { errno = ENOMEM; return NULL; }
    size_t total = count * bytes;
    void *pointer = malloc(total);
    if (pointer) memset(pointer, 0, total);
    return pointer;
}
void *realloc(void *pointer, size_t bytes)
{
    if (!pointer) return malloc(bytes);
    if (!bytes) { free(pointer); return NULL; }
    size_t rounded;
    if (!allocation_size(bytes, &rounded)) return NULL;
    struct block *block = (struct block *)pointer - 1;
    if (block->bytes < rounded && block->next && !block->next->requested &&
        block->bytes + sizeof *block + block->next->bytes >= rounded) join_next(block);
    if (block->bytes >= rounded) {
        split(block, rounded); block->requested = bytes; return pointer;
    }
    void *replacement = malloc(bytes);
    if (!replacement) return NULL;
    memcpy(replacement, pointer, block->requested);
    free(pointer); return replacement;
}
