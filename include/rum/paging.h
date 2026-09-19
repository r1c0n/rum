#ifndef RUM_PAGING_H
#define RUM_PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rum/pmm.h>

#define PAGING_WRITABLE 0x02u
#define PAGING_DYNAMIC_BASE RUM_HEAP_BASE

struct paging_space;

struct paging_statistics {
    uint32_t spaces, directory_pages, shared_table_pages;
    uint32_t kernel_directory, active_directory;
    uint32_t private_table_pages, user_pages;
};

struct paging_space_statistics {
    uint32_t directory, private_table_pages, user_pages;
    bool kernel;
};

/* Allocation-free, IRQ-safe snapshot. Shared table frames are counted once,
   not once per directory; zero before production paging initialization. */
struct paging_statistics paging_stats(void);

/* Startup only: identity-map RAM window, protect text/rodata, guard page zero. */
bool paging_initialize(void);
struct paging_space *paging_kernel_space(void);
struct paging_space *paging_active_space(void);
uint32_t paging_directory_address(const struct paging_space *space);
/* Allocation-free ownership snapshot for one registered address space. */
bool paging_space_stats(const struct paging_space *space,
                        struct paging_space_statistics *statistics);
/* Foreground only, after heap initialization. Own a new directory and metadata;
   borrow all kernel tables. No user mappings are created. NULL on failure. */
struct paging_space *paging_space_create(void);
/* Refuse the kernel, active/unregistered spaces, and unsupported private entries.
   Free only the inactive directory and metadata, never borrowed kernel tables. */
bool paging_space_destroy(struct paging_space *space);
/* Switch CR3 while preserving interrupt flags and discarding cached translations. */
bool paging_switch_space(struct paging_space *space);
/* Kernel-only 4 KiB aliases in [RUM_HEAP_BASE, RUM_KERNEL_STACK_BASE).
   The heap owns its subrange; other callers start at RUM_KERNEL_ALIAS_BASE.
   Backed by allocated RAM; reserved stack/user ranges are rejected.
   Only the kernel space may change shared mappings, even while another space
   is active. Never overwrite mappings. Data frames remain owned by the caller. */
bool paging_map_page(struct paging_space *space, uint32_t virtual, uint32_t physical, uint32_t flags);
bool paging_unmap_page(struct paging_space *space, uint32_t virtual, uint32_t *physical);
bool paging_translate(const struct paging_space *space, uint32_t virtual, uint32_t *physical);

/* Shared supervisor kernel-entry stacks. A slot owns four independently
   allocated, zeroed frames above an unmapped guard page. Allocation and release
   are transactional; only an inactive stack may be released by its owner. */
bool paging_kernel_stack_allocate(uint32_t slot);
bool paging_kernel_stack_release(uint32_t slot);

/* Private anonymous user pages. The address and page count must fit entirely
   inside the program range or the fixed user-stack range. Allocate zeroes every
   frame before publishing it and rolls the whole request back on failure.
   Release owns and frees the mapped frames; protect changes only write access. */
bool paging_user_allocate(struct paging_space *space, uint32_t virtual,
                          uint32_t pages, uint32_t flags);
bool paging_user_protect(struct paging_space *space, uint32_t virtual,
                         uint32_t pages, uint32_t flags);
bool paging_user_release(struct paging_space *space, uint32_t virtual, uint32_t pages);
uint32_t paging_user_page_count(const struct paging_space *space);

/* Validate every covered page before copying. A zero-byte copy succeeds without
   inspecting either address. String copies require a NUL within capacity and
   report its length excluding the NUL; failure leaves destination/length alone. */
bool paging_user_accessible(const struct paging_space *space, uint32_t address,
                            size_t bytes, bool writable);
bool paging_copy_from_user(const struct paging_space *space, void *destination,
                           uint32_t source, size_t bytes);
bool paging_copy_to_user(const struct paging_space *space, uint32_t destination,
                         const void *source, size_t bytes);
bool paging_copy_string_from_user(const struct paging_space *space, char *destination,
                                  size_t capacity, uint32_t source, size_t *length);

#endif
