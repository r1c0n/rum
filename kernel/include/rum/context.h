#ifndef RUM_CONTEXT_H
#define RUM_CONTEXT_H
#include <stddef.h>
#include <stdint.h>

/* i386 C callee-saved registers and return address. Interrupt frames are
   separate; this switches kernel continuations with IF clear, including those
   suspended at a trusted return to user mode. */
struct kernel_context {
    uint32_t edi, esi, ebx, ebp, eip, return_address;
};
_Static_assert(sizeof(struct kernel_context) == 24, "initial context size");
_Static_assert(offsetof(struct kernel_context, eip) == 16, "context return offset");
void kernel_context_switch(uint32_t *previous_stack, uint32_t next_stack);
#endif
