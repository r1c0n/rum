#ifndef RUM_INTERRUPTS_H
#define RUM_INTERRUPTS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <rum/cpu_layout.h>

/* Common assembly prefix. A ring-0 entry ends here; a ring-3 entry has the
   CPU's privilege-change ESP/SS pair immediately after this prefix. */
struct exception_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, saved_esp, ebx, edx, ecx, eax;
    uint32_t vector, error, eip, cs, eflags;
};

struct exception_user_frame {
    struct exception_frame core;
    uint32_t esp, ss;
};

_Static_assert(offsetof(struct exception_frame, vector) == 48, "ISR vector offset");
_Static_assert(offsetof(struct exception_frame, eip) == 56, "ISR instruction offset");
_Static_assert(sizeof(struct exception_frame) == 68, "ISR frame size");
_Static_assert(offsetof(struct exception_user_frame, esp) == 68, "User ESP offset");
_Static_assert(offsetof(struct exception_user_frame, ss) == 72, "User SS offset");
_Static_assert(sizeof(struct exception_user_frame) == 76, "User ISR frame size");

static inline bool exception_frame_from_user(const struct exception_frame *frame)
{
    return (frame->cs & 3) == 3;
}

static inline uint32_t exception_frame_esp(const struct exception_frame *frame)
{
    if (exception_frame_from_user(frame))
        return ((const struct exception_user_frame *)frame)->esp;
    /* PUSHAD saved ESP points at vector/error before the five-word core. */
    return frame->saved_esp + 5 * sizeof(uint32_t);
}

static inline uint32_t exception_frame_ss(const struct exception_frame *frame)
{
    if (exception_frame_from_user(frame))
        return ((const struct exception_user_frame *)frame)->ss & 0xFFFF;
    return KERNEL_DATA_SELECTOR;
}

void idt_initialize(void);
void interrupt_dispatch(struct exception_frame *frame);
void irq_register(uint8_t irq, void (*handler)(void));
void irq_dispatch(const struct exception_frame *frame);
uint32_t irq_spurious_count(void);
bool irq_in_handler(void);
_Noreturn void exception_dispatch(const struct exception_frame *frame);

#endif
