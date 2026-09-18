#include <rum/cpu.h>
#include <rum/cpu_policy.h>
#include <rum/gdt.h>

void cpu_initialize(void)
{
    gdt_initialize();
    /* Integer-only until tasks own and preserve extended CPU state. These
       controls fault actual x87/MMX/SIMD instructions from loaded code. */
    uint32_t control;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(control));
    control |= CPU_INTEGER_CR0;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(control) : "memory");
    __asm__ volatile ("mov %%cr4, %0" : "=r"(control));
    control &= ~CPU_UNSUPPORTED_CR4;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(control) : "memory");
}
