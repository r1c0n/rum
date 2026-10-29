#ifndef RUM_GDT_H
#define RUM_GDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rum/cpu_layout.h>

struct gdt_entry {
    uint16_t limit_low, base_low;
    uint8_t base_middle, access, limit_flags, base_high;
} __attribute__((packed));

struct gdt_descriptor {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct i386_tss {
    uint16_t previous, reserved0;
    uint32_t esp0;
    uint16_t ss0, reserved1;
    uint32_t esp1;
    uint16_t ss1, reserved2;
    uint32_t esp2;
    uint16_t ss2, reserved3;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es, reserved4, cs, reserved5, ss, reserved6, ds, reserved7;
    uint16_t fs, reserved8, gs, reserved9, ldt, reserved10;
    uint16_t trap, iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct gdt_entry) == 8, "GDT descriptor size");
_Static_assert(offsetof(struct gdt_entry, access) == 5, "GDT access offset");
_Static_assert(sizeof(struct gdt_descriptor) == 6, "GDTR operand size");
_Static_assert(offsetof(struct gdt_descriptor, base) == 2, "GDTR base offset");
_Static_assert(sizeof(struct i386_tss) == 104, "32-bit TSS size");
_Static_assert(offsetof(struct i386_tss, esp0) == 4, "TSS ESP0 offset");
_Static_assert(offsetof(struct i386_tss, ss0) == 8, "TSS SS0 offset");
_Static_assert(offsetof(struct i386_tss, cr3) == 28, "TSS CR3 offset");
_Static_assert(offsetof(struct i386_tss, iomap_base) == 102, "TSS I/O-map offset");

extern struct gdt_entry rum_gdt[GDT_ENTRY_COUNT];
extern struct i386_tss rum_tss;
extern struct i386_tss rum_double_fault_tss;
void gdt_initialize(void); /* Bootstrap only; preserves the Multiboot arguments. */
void gdt_load(const struct gdt_descriptor *descriptor);
/* Caller owns a mapped supervisor stack. Its top must be nonzero and aligned. */
bool gdt_set_kernel_stack(uint32_t top);
/* Keep the software-scheduled task's ring-zero stack and CR3 in the hardware
   TSS so a nested task switch can preserve a complete interrupted context. */
bool gdt_set_kernel_context(uint32_t top, uint32_t directory);
/* Configure the hardware task-gate target after its guarded stack and kernel
   CR3 exist. The target never returns to the interrupted task. */
bool gdt_set_double_fault_stack(uint32_t top, uint32_t kernel_directory);

#endif
