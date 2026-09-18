/* Isolated CPU-register fixtures. The original #PF fixture keeps its tiny
   table layout; paging-kernel.c tests the separate production memory path. */
#include <stdint.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/serial.h>
#include <rum/terminal.h>

void kernel_main(uint32_t magic, uint32_t information);
_Noreturn void fault_trigger_de(void);
_Noreturn void fault_trigger_ud(void);
_Noreturn void fault_trigger_gp(void);
_Noreturn void fault_trigger_pf(void);

#if RUM_FAULT_CASE == 3
static uint32_t directory[1024] __attribute__((aligned(4096)));
static uint32_t table[1024] __attribute__((aligned(4096)));

static void enable_test_paging(void)
{
    /* Map the first 4 MiB (test kernel, VGA, stack, GDT, IDT), leave 4 MiB
       unmapped, then perform a supervisor read there to generate a real #PF. */
    for (unsigned i = 0; i < 1024; ++i)
        table[i] = i * 4096 | 3;
    directory[0] = (uintptr_t)table | 3;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uintptr_t)directory) : "memory");
    uint32_t control;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(control));
    control |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(control) : "memory");
}
#endif

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    if (magic != 0x2BADB002 || information == 0) {
        serial_writestring("rum_fault_test_bad_handoff\n");
        cpu_halt();
    }
#if RUM_FAULT_CASE == 0
    fault_trigger_de();
#elif RUM_FAULT_CASE == 1
    fault_trigger_ud();
#elif RUM_FAULT_CASE == 2
    fault_trigger_gp();
#elif RUM_FAULT_CASE == 3
    enable_test_paging();
    fault_trigger_pf();
#else
#error "Unknown fault test"
#endif
}
