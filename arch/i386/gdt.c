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
struct i386_tss rum_double_fault_tss __attribute__((aligned(16)));
extern const char __boot_stack_top[];
_Noreturn void double_fault_entry(void);
static bool initialized;

static struct gdt_entry tss_descriptor(const struct i386_tss *tss)
{
    uint32_t base = (uintptr_t)tss;
    return (struct gdt_entry){
        .limit_low = sizeof *tss - 1,
        .base_low = (uint16_t)base,
        .base_middle = (uint8_t)(base >> 16),
        .access = 0x89, /* Present, available 32-bit TSS, DPL 0. */
        .base_high = (uint8_t)(base >> 24),
    };
}

void gdt_initialize(void)
{
    if (initialized) return;
    memset(&rum_tss, 0, sizeof rum_tss);
    memset(&rum_double_fault_tss, 0, sizeof rum_double_fault_tss);
    rum_tss.ss0 = KERNEL_DATA_SELECTOR;
    rum_tss.esp0 = (uintptr_t)__boot_stack_top;
    /* Beyond the descriptor limit: deny all user I/O when CPL > IOPL. */
    rum_tss.iomap_base = sizeof rum_tss;
    rum_gdt[TSS_SELECTOR >> 3] = tss_descriptor(&rum_tss);
    rum_gdt[DOUBLE_FAULT_TSS_SELECTOR >> 3] = tss_descriptor(&rum_double_fault_tss);
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

bool gdt_set_kernel_context(uint32_t top, uint32_t directory)
{
    if (!initialized || !top || top % RUM_STACK_ALIGNMENT ||
        !directory || directory % RUM_PAGE_SIZE) return false;
    uint32_t saved = cpu_interrupt_save();
    rum_tss.esp0 = top;
    /* Software scheduling changes CR3 without a hardware task switch. Keep the
       normal TSS synchronized for the nested double-fault task switch. */
    rum_tss.cr3 = directory;
    cpu_interrupt_restore(saved);
    return true;
}

bool gdt_set_double_fault_stack(uint32_t top, uint32_t kernel_directory)
{
    if (!initialized || !top || top % RUM_STACK_ALIGNMENT ||
        !kernel_directory || kernel_directory % RUM_PAGE_SIZE) return false;
    uint32_t saved = cpu_interrupt_save();
    memset(&rum_double_fault_tss, 0, sizeof rum_double_fault_tss);
    rum_double_fault_tss.esp0 = top;
    rum_double_fault_tss.ss0 = KERNEL_DATA_SELECTOR;
    rum_double_fault_tss.cr3 = kernel_directory;
    rum_double_fault_tss.eip = (uintptr_t)double_fault_entry;
    rum_double_fault_tss.eflags = 2;
    rum_double_fault_tss.esp = top;
    rum_double_fault_tss.cs = KERNEL_CODE_SELECTOR;
    rum_double_fault_tss.ss = KERNEL_DATA_SELECTOR;
    rum_double_fault_tss.ds = KERNEL_DATA_SELECTOR;
    rum_double_fault_tss.es = KERNEL_DATA_SELECTOR;
    rum_double_fault_tss.fs = KERNEL_DATA_SELECTOR;
    rum_double_fault_tss.gs = KERNEL_DATA_SELECTOR;
    rum_double_fault_tss.iomap_base = sizeof rum_double_fault_tss;
    rum_gdt[DOUBLE_FAULT_TSS_SELECTOR >> 3].access = 0x89;
    cpu_interrupt_restore(saved);
    return true;
}
