#ifndef RUM_PROCESS_LIMITS_H
#define RUM_PROCESS_LIMITS_H

#include <rum/memory_layout.h>
#include <rum/abi/process.h>
#include <rum/abi/layout.h>

/* Process resource policy shared by the loader, launcher, and task registry. */
#define RUM_PROCESS_LIMIT          16
#define RUM_PROCESS_USER_BYTES     RUM_ABI_MEMORY_BYTES
#define RUM_PROCESS_ARGUMENT_LIMIT RUM_ABI_ARGUMENT_LIMIT
#define RUM_PROCESS_ARGUMENT_BYTES RUM_ABI_ARGUMENT_BYTES
#define RUM_PROCESS_FILE_LIMIT     32         /* Includes standard streams. */
#define RUM_KERNEL_STACK_SLOTS     (RUM_PROCESS_LIMIT + 2) /* Idle and emergency. */

#ifndef __ASSEMBLER__
_Static_assert(RUM_PROCESS_LIMIT > 0 && RUM_PROCESS_ARGUMENT_LIMIT > 0 &&
               RUM_PROCESS_ARGUMENT_BYTES > 0 && RUM_PROCESS_FILE_LIMIT >= 3,
               "nonempty process limits");
_Static_assert(RUM_PROCESS_USER_BYTES >= RUM_USER_STACK_SIZE &&
               (RUM_PROCESS_USER_BYTES % RUM_PAGE_SIZE) == 0 &&
               RUM_PROCESS_USER_BYTES <= RUM_USER_END - RUM_USER_BASE,
               "user page budget includes stack");
_Static_assert(RUM_KERNEL_STACK_SLOTS * RUM_KERNEL_STACK_STRIDE <=
               RUM_KERNEL_STACK_END - RUM_KERNEL_STACK_BASE,
               "kernel stacks and guards fit reserved window");
#endif

#endif
