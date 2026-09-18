#include <rum/cpu.h>
#include <rum/gdt.h>
#include <rum/memory.h>
#include <rum/memory_layout.h>

/* Writable: loading TR marks the TSS descriptor busy. Segment accessed bits
   are preset; kernel and user code/data remain flat 32-bit segments. */
struct gdt_entry rum_gdt[GDT_ENTRY_COUNT] __attribute__((aligned(8))) = {
    {0},
    { .limit_low = 0xFFFF, .access = 0x9B, .limit_flags = 0xCF },
    { .limit_low = 0xFFFF, .access = 0x93, .limit_flags = 0xCF },
    { .limit_low = 0xFFFF, .access = 0xFB, .limit_flags = 0xCF },
    { .limit_low = 0xFFFF, .access = 0xF3, .limit_flags = 0xCF },
    {0},
};
struct i386_tss rum_tss __attribute__((aligned(16)));
extern const char __boot_stack_top[];
static bool initialized;

void gdt_initialize(void)
{
    if (initialized) return;
    memset(&rum_tss, 0, sizeof rum_tss);
    rum_tss.ss0 = KERNEL_DATA_SELECTOR;
    rum_tss.esp0 = (uintptr_t)__boot_stack_top;
    /* Beyond the descriptor limit: deny all user I/O when CPL > IOPL. */
    rum_tss.iomap_base = sizeof rum_tss;
    uint32_t base = (uintptr_t)&rum_tss;
    rum_gdt[TSS_SELECTOR >> 3] = (struct gdt_entry){
        .limit_low = sizeof rum_tss - 1,
        .base_low = (uint16_t)base,
        .base_middle = (uint8_t)(base >> 16),
        .access = 0x89, /* Present, available 32-bit TSS, DPL 0. */
        .base_high = (uint8_t)(base >> 24),
    };
    const struct gdt_descriptor descriptor = {
        .limit = sizeof rum_gdt - 1, .base = (uintptr_t)rum_gdt,
    };
    gdt_load(&descriptor);
    uint16_t selector = TSS_SELECTOR;
    __asm__ volatile ("ltr %0" : : "r"(selector) : "memory");
    initialized = true;
}

bool gdt_set_kernel_stack(uint32_t top)
{
    if (!initialized || !top || top % RUM_STACK_ALIGNMENT) return false;
    uint32_t saved = cpu_interrupt_save();
    rum_tss.esp0 = top;
    cpu_interrupt_restore(saved);
    return true;
}
