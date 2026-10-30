#ifndef RUM_CPU_H
#define RUM_CPU_H

#include <stdint.h>

void cpu_initialize(void); /* Bootstrap: GDT, TSS, integer-only CPU policy. */

static inline uint32_t cpu_interrupt_save(void)
{
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void cpu_interrupt_restore(uint32_t flags)
{
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static inline void cpu_interrupt_enable(void)
{
    __asm__ volatile ("sti" : : : "memory");
}

/* Call with IF clear after checking pending foreground work. STI's interrupt
   shadow covers HLT, preventing a queued event from being lost before sleep. */
static inline void cpu_idle(void)
{
    __asm__ volatile ("sti; hlt" : : : "memory");
}

_Noreturn void cpu_halt(void);

#endif
