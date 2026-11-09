#ifndef RUM_VM_H
#define RUM_VM_H

#include <stdbool.h>
#include <rum/abi/memory.h>

enum vm_region_owner { VM_REGION_LOADER, VM_REGION_STACK, VM_REGION_DYNAMIC };
struct vm_region {
    uint32_t address, bytes, protection;
    enum vm_region_owner owner;
};
struct vm_registry {
    uint32_t count;
    struct vm_region regions[RUM_VM_REGION_LIMIT];
};

/* Allocation-free policy over a trusted, bounded snapshot. These functions do
   not map pages, retain pointers, change packets or publish regions. Output is
   written only on success. The caller must serialize snapshot through commit;
   validation alone does not reserve a range or physical memory. */
bool vm_pages_fit(uint32_t committed, uint32_t additional);
rum_result_t vm_prepare_allocate(const struct rum_vm_allocate_request *,
                                 const struct vm_region *, uint32_t count,
                                 uint32_t committed_pages, struct vm_region *);
rum_result_t vm_prepare_protect(const struct rum_vm_protect_request *,
                                const struct vm_region *, uint32_t count,
                                struct vm_region *);
rum_result_t vm_prepare_release(const struct rum_vm_release_request *,
                                const struct vm_region *, uint32_t count,
                                struct vm_region *);

/* Embedded in the private address-space owner; no allocation per record. */
_Static_assert(sizeof(struct vm_region) == 16, "bounded region metadata");
#define VM_REGION_METADATA_CAPACITY (RUM_VM_REGION_LIMIT * sizeof(struct vm_region))
#endif
