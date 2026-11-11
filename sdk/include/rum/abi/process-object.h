#ifndef RUM_ABI_PROCESS_OBJECT_H
#define RUM_ABI_PROCESS_OBJECT_H
#include <rum/abi/object.h>
#include <rum/abi/process.h>
#define RUM_PROCESS_OBJECT_VERSION 1u
#define RUM_PROCESS_OBJECT_OPEN 1u
#define RUM_PROCESS_OBJECT_QUERY 2u
#define RUM_PROCESS_OBJECT_WAIT 3u
#define RUM_PROCESS_OBJECT_TERMINATE 4u
#define RUM_PROCESS_OPEN_SESSION 1u /* Existing foreground command delegation. */
#define RUM_PROCESS_OPEN_INHERIT 2u
#define RUM_PROCESS_OBJECT_NOHANG 1u
#ifndef __ASSEMBLER__
/* OPEN: pid/rights/flags input, result_handle output, handle/data zero.
   QUERY/WAIT: handle/data input. WAIT accepts NOHANG; QUERY has zero flags.
   TERMINATE: handle only. All unused/reserved fields must be zero.
   WAIT observes retained status without collecting the legacy child result. */
struct rum_process_object_request {
    uint32_t version, size, operation;
    rum_object_handle_t handle;
    rum_pid_t process_id;
    uint32_t rights, flags;
    rum_address_t data;
    rum_object_handle_t result_handle;
    uint32_t reserved[3];
};
/* Sanitized snapshot. No address-space, stack, fault-address or task pointers.
   Parent is captured when the reference is first opened; zero means kernel. */
struct rum_process_object_information {
    uint32_t version, size;
    rum_pid_t process_id, parent_id;
    uint32_t state, flags, termination;
    rum_result_t status;
    uint32_t fault_vector, reserved[3];
    char name[RUM_PROCESS_NAME_CAPACITY];
};
_Static_assert(sizeof(struct rum_process_object_request) == 48 &&
               offsetof(struct rum_process_object_request, result_handle) == 32 &&
               sizeof(struct rum_process_object_information) == 112, "process object ABI layout");
#endif
#endif
