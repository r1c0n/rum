#ifndef RUM_ABI_OBJECT_H
#define RUM_ABI_OBJECT_H
#include <rum/abi/types.h>

#define RUM_OBJECT_ABI_VERSION 1u
#define RUM_OBJECT_HANDLE_LIMIT 32u
#define RUM_OBJECT_INVALID_HANDLE 0u
/* Separate from stream descriptors. Tokens are process-local, not pointers;
   all 32 bits matter and callers must not interpret the encoded generation. */

#define RUM_OBJECT_ANY 0u
#define RUM_OBJECT_FILE 1u
#define RUM_OBJECT_DIRECTORY 2u
#define RUM_OBJECT_PIPE_READER 3u
#define RUM_OBJECT_PIPE_WRITER 4u
#define RUM_OBJECT_CONSOLE_INPUT 5u
#define RUM_OBJECT_CONSOLE_OUTPUT 6u
#define RUM_OBJECT_PROCESS 7u

#define RUM_RIGHT_QUERY       0x001u
#define RUM_RIGHT_DUPLICATE   0x002u
#define RUM_RIGHT_READ        0x004u
#define RUM_RIGHT_WRITE       0x008u
#define RUM_RIGHT_SEEK        0x010u
#define RUM_RIGHT_ENUMERATE   0x020u
#define RUM_RIGHT_FLUSH       0x040u
#define RUM_RIGHT_WAIT        0x080u
#define RUM_RIGHT_TERMINATE   0x100u
#define RUM_RIGHT_MASK        0x1ffu
#define RUM_RIGHT_COMMON (RUM_RIGHT_QUERY | RUM_RIGHT_DUPLICATE)
#define RUM_RIGHT_FILE (RUM_RIGHT_COMMON | RUM_RIGHT_READ | RUM_RIGHT_WRITE | RUM_RIGHT_SEEK | RUM_RIGHT_FLUSH)
#define RUM_RIGHT_DIRECTORY (RUM_RIGHT_COMMON | RUM_RIGHT_ENUMERATE)
#define RUM_RIGHT_INPUT (RUM_RIGHT_COMMON | RUM_RIGHT_READ)
#define RUM_RIGHT_OUTPUT (RUM_RIGHT_COMMON | RUM_RIGHT_WRITE)
#define RUM_RIGHT_PROCESS (RUM_RIGHT_COMMON | RUM_RIGHT_WAIT | RUM_RIGHT_TERMINATE)

#define RUM_OBJECT_INHERIT 1u /* Explicit opt-in; default is non-inheritable. */
#define RUM_OBJECT_QUERY 1u
#define RUM_OBJECT_DUPLICATE 2u
#define RUM_OBJECT_CLOSE 3u
#ifndef __ASSEMBLER__
#include <stddef.h>
typedef uint32_t rum_object_handle_t;
/* QUERY: handle/type input; rights/flags/result_handle zero. Fills type,
   rights and flags. DUPLICATE: rights/flags input; fills result_handle.
   CLOSE: rights/flags/result_handle zero; consumes only this handle.
   type is ANY or an expected concrete type, checked before the operation.
   All calls return zero/negative error; unsigned tokens use packet output. */
struct rum_object_request {
    uint32_t version, size, operation;
    rum_object_handle_t handle;
    uint32_t type, rights, flags;
    rum_object_handle_t result_handle;
    uint32_t reserved[4];
};
struct rum_object_information {
    uint32_t version, size, type, rights, flags, reserved[3];
};
_Static_assert(sizeof(struct rum_object_request) == 48 && offsetof(struct rum_object_request, result_handle) == 28,
               "fixed object packet layout");
_Static_assert(sizeof(struct rum_object_information) == 32, "fixed object query information");
#endif
#endif
