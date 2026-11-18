#ifndef RUM_PROCESS_LIMITS_H
#define RUM_PROCESS_LIMITS_H

#include <rum/memory_layout.h>
#include <rum/abi/process.h>
#include <rum/abi/layout.h>
#include <rum/abi/filesystem.h>

/* Process resource policy shared by the loader, launcher, and task registry. */
#define RUM_PROCESS_LIMIT          16
#define RUM_PROCESS_USER_BYTES     RUM_ABI_MEMORY_BYTES
#define RUM_PROCESS_ARGUMENT_LIMIT RUM_ABI_ARGUMENT_LIMIT
#define RUM_PROCESS_ARGUMENT_BYTES RUM_ABI_ARGUMENT_BYTES
#define RUM_PROCESS_FILE_LIMIT     RUM_ABI_HANDLE_LIMIT /* Includes streams. */
/* Non-permanent execution contexts share this budget, including kernel workers.
   The current one-thread launcher still uses the smaller process registry. */
#define RUM_SYSTEM_THREAD_LIMIT    32
#define RUM_PROCESS_THREAD_LIMIT   4 /* Includes the initial thread. */
#define RUM_KERNEL_STACK_SLOTS     (RUM_SYSTEM_THREAD_LIMIT + 2) /* Idle and emergency. */
#define RUM_THREAD_TLS_SIZE        RUM_PAGE_SIZE
#define RUM_USER_THREAD_STRIDE     (RUM_USER_STACK_SIZE + RUM_PAGE_SIZE + RUM_THREAD_TLS_SIZE)

#ifndef __ASSEMBLER__
_Static_assert(RUM_PROCESS_LIMIT == RUM_PROCESS_LIST_LIMIT, "public snapshot covers the full registry");
_Static_assert(RUM_PROCESS_LIMIT > 0 && RUM_PROCESS_ARGUMENT_LIMIT > 0 &&
               RUM_PROCESS_ARGUMENT_BYTES > 0 && RUM_PROCESS_FILE_LIMIT >= 3,
               "nonempty process limits");
_Static_assert(RUM_PROCESS_USER_BYTES >= RUM_USER_STACK_SIZE &&
               (RUM_PROCESS_USER_BYTES % RUM_PAGE_SIZE) == 0 &&
               RUM_PROCESS_USER_BYTES <= RUM_USER_END - RUM_USER_BASE,
               "user page budget includes stack");
_Static_assert(RUM_PROCESS_LIMIT <= RUM_SYSTEM_THREAD_LIMIT &&
               RUM_PROCESS_THREAD_LIMIT > 1 && RUM_PROCESS_THREAD_LIMIT <= RUM_SYSTEM_THREAD_LIMIT,
               "thread capacity includes existing launchers");
_Static_assert(RUM_KERNEL_STACK_SLOTS <=
               (RUM_KERNEL_STACK_END - RUM_KERNEL_STACK_BASE) / RUM_KERNEL_STACK_STRIDE,
               "kernel stacks and guards fit reserved window");
_Static_assert(RUM_PROCESS_THREAD_LIMIT <=
               (RUM_USER_END - RUM_USER_STACK_WINDOW_BASE) / RUM_USER_THREAD_STRIDE &&
               RUM_PROCESS_THREAD_LIMIT <= RUM_PROCESS_USER_BYTES / (RUM_USER_STACK_SIZE + RUM_THREAD_TLS_SIZE),
               "user stacks, guards and TLS fit the address and commit budgets");

/* From low to high each slot reserves TLS, guard, downward-growing stack.
   Validate before multiplying. Only the initial stack is currently mapped. */
static inline bool thread_stack_top(uint32_t slot, uint32_t *top)
{
    if (!top || slot >= RUM_PROCESS_THREAD_LIMIT) return false;
    *top = RUM_USER_STACK_TOP - slot * RUM_USER_THREAD_STRIDE;
    return true;
}
#endif

#endif
