#ifndef RUM_OBJECT_HANDLE_H
#define RUM_OBJECT_HANDLE_H
#include <stdbool.h>
#include <rum/abi/object.h>
#include <rum/object.h>

/* Five slot bits, 26 nonzero generation bits and a namespace tag. Exhausted
   slots retire permanently in this process; closing never resets generation. */
#define OBJECT_HANDLE_TAG 0x80000000u
#define OBJECT_HANDLE_GENERATION_MAX 0x03ffffffu
struct object_handle_entry {
    struct kernel_object *object;
    uint32_t generation, rights, flags;
    bool closing;
};
struct object_handles { struct object_handle_entry entries[RUM_OBJECT_HANDLE_LIMIT]; };

/* Trusted authority boundary. A prepared, published OWNER reference transfers
   only on success; granted must be a type-valid subset of authorized rights.
   Caller exclusively owns this table, or holds a short update scope. */
rum_result_t object_handle_install(struct object_handles *, struct kernel_object *,
                                  uint32_t authorized, uint32_t granted, uint32_t flags, rum_object_handle_t *);
/* Kernel operation acquisition validates token, expected type and required
   rights before returning an OPERATION reference. Release through object core. */
rum_result_t object_handle_acquire(struct object_handles *, rum_object_handle_t, uint32_t type,
                                  uint32_t required, struct kernel_object **);
rum_result_t object_handle_request(struct object_handles *, struct rum_object_request *);
rum_result_t object_request_validate(const struct rum_object_request *);
/* New private empty destination only; preserve numeric inherited tokens and
   source generation history. Rights can only shrink. Publication is all-or-
   nothing; a failure releases every acquired reference. No backend callouts. */
rum_result_t object_handles_inherit(struct object_handles *, const struct object_handles *, uint32_t rights_ceiling);
/* Close all entries; errors keep unclosed entries owned for retry. */
rum_result_t object_handles_destroy(struct object_handles *);
unsigned object_handles_count(const struct object_handles *);
uint32_t object_rights_mask(uint32_t type);
#endif
