#ifndef RUM_CPU_POLICY_H
#define RUM_CPU_POLICY_H

/* Fresh user flags: reserved bit 1, IF enabled, IOPL zero, DF/TF/NT/VM clear. */
#define CPU_USER_EFLAGS 0x202
#define CPU_INTEGER_CR0 0xE /* MP | EM | TS */
#define CPU_UNSUPPORTED_CR4 0x40600 /* OSFXSR | OSXMMEXCPT | OSXSAVE */

#ifndef __ASSEMBLER__
#include <rum/interrupts.h>

/* Trusted kernel caller supplies validated entry/stack addresses. No live
   kernel EFLAGS or registers are copied into a new user context. */
static inline void cpu_user_frame_initialize(struct exception_user_frame *frame,
                                             uint32_t entry, uint32_t stack)
{
    *frame = (struct exception_user_frame){
        .core = {
            .gs = USER_DATA_SELECTOR, .fs = USER_DATA_SELECTOR,
            .es = USER_DATA_SELECTOR, .ds = USER_DATA_SELECTOR,
            .eip = entry, .cs = USER_CODE_SELECTOR, .eflags = CPU_USER_EFLAGS,
        },
        .esp = stack, .ss = USER_DATA_SELECTOR,
    };
}
#endif
#endif
