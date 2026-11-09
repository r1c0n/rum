#ifndef RUM_ABI_MEMORY_H
#define RUM_ABI_MEMORY_H

#include <rum/abi/layout.h>
#include <rum/abi/types.h>

/* Region contracts; syscall entry points are introduced separately. */
#define RUM_VM_ABI_VERSION 1
#define RUM_VM_REGION_LIMIT 64 /* Includes loader and stack regions. */
#define RUM_VM_READ  1u
#define RUM_VM_WRITE 2u
#define RUM_VM_PROTECTION_MASK (RUM_VM_READ | RUM_VM_WRITE)

#ifndef __ASSEMBLER__
#include <stddef.h>

/* address=0 selects the lowest free range; otherwise it is an exact, aligned
   address, never permission to replace a mapping. bytes is rounded up to pages.
   Success returns the selected address and rounded bytes in the same packet.
   READ is mandatory; WRITE is optional. Plain i386 paging cannot enforce NX.
   flags and reserved words must be zero in all three request types. */
struct rum_vm_allocate_request {
    uint32_t version, size;
    rum_address_t address;
    rum_size_t bytes;
    uint32_t protection, flags, reserved[2];
};

/* Protect/release require the exact address and rounded size returned by
   allocate. Neither operation may change loader or stack mappings. */
struct rum_vm_protect_request {
    uint32_t version, size;
    rum_address_t address;
    rum_size_t bytes;
    uint32_t protection, flags, reserved[2];
};

struct rum_vm_release_request {
    uint32_t version, size;
    rum_address_t address;
    rum_size_t bytes;
    uint32_t flags, reserved[3];
};

_Static_assert(sizeof(struct rum_vm_allocate_request) == 32 &&
               sizeof(struct rum_vm_protect_request) == 32 &&
               sizeof(struct rum_vm_release_request) == 32,
               "fixed-width memory packets");
_Static_assert(offsetof(struct rum_vm_allocate_request, address) == 8 &&
               offsetof(struct rum_vm_allocate_request, bytes) == 12 &&
               offsetof(struct rum_vm_allocate_request, reserved) == 24 &&
               offsetof(struct rum_vm_protect_request, protection) == 16 &&
               offsetof(struct rum_vm_release_request, reserved) == 20,
               "memory packet offsets");
#endif
#endif
