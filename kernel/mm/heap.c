#include <rum/cpu.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/paging.h>

struct block {
    size_t size;
    struct block *previous, *next;
    bool free;
} __attribute__((aligned(HEAP_ALIGNMENT)));

_Static_assert(sizeof(struct block) % HEAP_ALIGNMENT == 0, "heap header alignment");
static struct block *heap_first, *heap_last;
static size_t heap_mapped;
static bool ready;

static size_t aligned(size_t size)
{
    if (!size || size > HEAP_LIMIT - sizeof(struct block)) return 0;
    return (size + HEAP_ALIGNMENT - 1) & ~(size_t)(HEAP_ALIGNMENT - 1);
}

static bool grow(size_t bytes)
{
    size_t growth = (bytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    if (!growth || growth > HEAP_LIMIT - heap_mapped) return false;
    size_t added = 0;
    while (added < growth) {
        uint32_t physical = pmm_allocate_page();
        if (!physical) break;
        if (!paging_map_page(paging_kernel_space(), HEAP_BASE + heap_mapped + added, physical, PAGING_WRITABLE)) {
            (void)pmm_free_page(physical);
            break;
        }
        added += PAGE_SIZE;
    }
    if (added != growth) {
        while (added) {
            added -= PAGE_SIZE;
            uint32_t physical;
            if (paging_unmap_page(paging_kernel_space(), HEAP_BASE + heap_mapped + added, &physical))
                (void)pmm_free_page(physical);
        }
        return false;
    }
    if (heap_last && heap_last->free) {
        heap_last->size += growth;
    } else {
        struct block *block = (void *)(uintptr_t)(HEAP_BASE + heap_mapped);
        *block = (struct block){ .size = growth - sizeof *block,
                                 .previous = heap_last, .free = true };
        if (heap_last) heap_last->next = block;
        else heap_first = block;
        heap_last = block;
    }
    heap_mapped += growth;
    return true;
}

static struct block *find(void *pointer)
{
    for (struct block *block = heap_first; block; block = block->next)
        if ((void *)(block + 1) == pointer && !block->free) return block;
    return NULL;
}

static void join_next(struct block *block)
{
    struct block *next = block->next;
    block->size += sizeof *next + next->size;
    block->next = next->next;
    if (block->next) block->next->previous = block;
    else heap_last = block;
}

static void split(struct block *block, size_t size)
{
    if (block->size - size < sizeof *block + HEAP_ALIGNMENT) return;
    struct block *rest = (void *)((unsigned char *)(block + 1) + size);
    *rest = (struct block){ .size = block->size - size - sizeof *block,
                           .previous = block, .next = block->next, .free = true };
    if (rest->next) rest->next->previous = rest;
    else heap_last = rest;
    block->next = rest;
    block->size = size;
    if (rest->next && rest->next->free) join_next(rest);
}

static void *allocate(size_t size)
{
    if (!ready || !(size = aligned(size))) return NULL;
    for (struct block *block = heap_first; block; block = block->next) {
        if (block->free && block->size >= size) {
            split(block, size);
            block->free = false;
            return block + 1;
        }
    }
    size_t needed = heap_last->free ? size - heap_last->size : size + sizeof *heap_last;
    if (!grow(needed)) return NULL;
    struct block *block = heap_last;
    split(block, size);
    block->free = false;
    return block + 1;
}

static void release(struct block *block)
{
    block->free = true;
    if (block->next && block->next->free) join_next(block);
    if (block->previous && block->previous->free) join_next(block->previous);
}

bool heap_initialize(void)
{
    uint32_t flags = cpu_interrupt_save();
    bool success = !ready && grow(PAGE_SIZE);
    if (success) ready = true;
    cpu_interrupt_restore(flags);
    return success;
}

void *kmalloc(size_t size)
{
    uint32_t flags = cpu_interrupt_save();
    void *pointer = allocate(size);
    cpu_interrupt_restore(flags);
    return pointer;
}

void *kcalloc(size_t count, size_t size)
{
    if (!count || !size || count > SIZE_MAX / size) return NULL;
    void *pointer = kmalloc(count * size);
    if (pointer) memset(pointer, 0, count * size);
    return pointer;
}

bool kfree(void *pointer)
{
    if (!pointer) return true;
    uint32_t flags = cpu_interrupt_save();
    struct block *block = find(pointer);
    if (block) release(block);
    cpu_interrupt_restore(flags);
    return block != NULL;
}

void *krealloc(void *pointer, size_t size)
{
    if (!pointer) return kmalloc(size);
    if (!size) { (void)kfree(pointer); return NULL; }
    uint32_t flags = cpu_interrupt_save();
    struct block *block = find(pointer);
    size_t wanted = aligned(size);
    void *result = NULL;
    if (block && wanted) {
        if (wanted <= block->size) {
            split(block, wanted);
            result = pointer;
        } else if (block->next && block->next->free &&
                   block->size + sizeof *block + block->next->size >= wanted) {
            join_next(block);
            split(block, wanted);
            result = pointer;
        } else {
            result = allocate(wanted);
            if (result) { memcpy(result, pointer, block->size); release(block); }
        }
    }
    cpu_interrupt_restore(flags);
    return result;
}

struct heap_statistics heap_stats(void)
{
    uint32_t flags = cpu_interrupt_save();
    struct heap_statistics stats = { .mapped_bytes = heap_mapped };
    for (struct block *block = heap_first; block; block = block->next) {
        if (block->free) stats.free_bytes += block->size;
        else { stats.used_bytes += block->size; ++stats.allocations; }
    }
    cpu_interrupt_restore(flags);
    return stats;
}
