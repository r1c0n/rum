#ifndef RUM_INTERRUPTS_H
#define RUM_INTERRUPTS_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10

/* Layout built by interrupts.s. Only ring 0 is supported at this milestone:
   the CPU pushes EIP, CS and EFLAGS, without a privilege-change SS/ESP pair. */
struct exception_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, saved_esp, ebx, edx, ecx, eax;
    uint32_t vector, error, eip, cs, eflags;
};

_Static_assert(offsetof(struct exception_frame, vector) == 48, "ISR vector offset");
_Static_assert(offsetof(struct exception_frame, eip) == 56, "ISR instruction offset");
_Static_assert(sizeof(struct exception_frame) == 68, "ISR frame size");

void idt_initialize(void);
_Noreturn void exception_dispatch(const struct exception_frame *frame);

#endif
