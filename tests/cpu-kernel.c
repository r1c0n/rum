/* No paging in this isolated fixture: test CPU privilege transitions before
   private user mappings and process recovery exist. Never linked into rum. */
#include <rum/cpu.h>
#include <rum/cpu_policy.h>
#include <rum/gdt.h>
#include <rum/memory_layout.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>
#include <rum/timer.h>

void kernel_main(uint32_t magic, uint32_t information);
void cpu_test_dispatch(struct exception_frame *frame);
_Noreturn void cpu_test_enter(const struct exception_user_frame *frame);
extern const char cpu_test_stack_bottom[], cpu_test_stack_top[], cpu_test_user_stack_top[];
extern const char cpu_test_user[], cpu_test_wait_start[], cpu_test_wait_end[];
extern const char cpu_test_done[], cpu_test_x87[], cpu_test_mmx[], cpu_test_sse[], cpu_test_io[];
extern const uint32_t cpu_test_observed[13], cpu_test_initial_flags, cpu_test_return_flags;
extern const uint32_t cpu_test_c_alignment;
volatile uint32_t cpu_test_irq_returns;
const uint32_t cpu_test_operation = RUM_CPU_CASE;
static const uint32_t expected_registers[7] = {
    0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00,
    0x13579BDF, 0x2468ACE0, 0x0BADF00D,
};

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_cpu_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static void report_hex(uint32_t number)
{
    static const char digits[] = "0123456789abcdef";
    char text[10] = "00000000 ";
    for (unsigned i = 0; i < 8; ++i)
        text[i] = digits[(number >> (28 - i * 4)) & 15];
    serial_writestring(text);
}

void cpu_test_dispatch(struct exception_frame *frame)
{
    uint32_t flags;
    uint16_t ds, ss;
    __asm__ volatile ("pushfl; popl %0; mov %%ds, %1; mov %%ss, %2"
                      : "=r"(flags), "=r"(ds), "=r"(ss));
    check(!(flags & 0x600) && ds == KERNEL_DATA_SELECTOR && ss == KERNEL_DATA_SELECTOR,
          "C entry clears DF/IF and loads kernel segments");
    check(cpu_test_c_alignment == 12, "16-byte stack alignment before C call");
    check(exception_frame_from_user(frame) && frame->cs == USER_CODE_SELECTOR,
          "saved user CS");
    check((uintptr_t)frame == (uintptr_t)cpu_test_stack_top - sizeof(struct exception_user_frame),
          "hardware TSS stack switch and frame size");
    check(exception_frame_ss(frame) == USER_DATA_SELECTOR, "saved user SS");
    check(frame->ds == USER_DATA_SELECTOR && frame->es == USER_DATA_SELECTOR &&
          frame->fs == USER_DATA_SELECTOR && frame->gs == USER_DATA_SELECTOR,
          "saved user segments");
    if (frame->vector == PIC_VECTOR_BASE) {
        /* An IRQ may arrive during the few instructions before the wait loop.
           Only the controlled window requires the exact sentinel stack/DF. */
        if (frame->eip >= (uintptr_t)cpu_test_wait_start && frame->eip < (uintptr_t)cpu_test_wait_end) {
            check(frame->eax == expected_registers[0] && frame->ebx == expected_registers[1] &&
                  frame->ecx == expected_registers[2] && frame->edx == expected_registers[3] &&
                  frame->esi == expected_registers[4] && frame->edi == expected_registers[5] &&
                  frame->ebp == expected_registers[6], "saved IRQ general registers");
            check(exception_frame_esp(frame) == (uintptr_t)cpu_test_user_stack_top,
                  "saved IRQ user ESP");
            check((frame->eflags & 0x3600) == 0x600, "saved IRQ IF/DF and IOPL zero");
            ++cpu_test_irq_returns;
        }
        irq_dispatch(frame); /* Real PIC acknowledgment, same as production. */
        return;
    }
    check(cpu_test_irq_returns >= 3 && timer_ticks() >= 3, "repeated real PIT delivery and EOI");
    check(cpu_test_initial_flags == CPU_USER_EFLAGS, "fresh initial user EFLAGS");
    check((cpu_test_return_flags & 0x3600) == 0x600, "IRET preserves user DF/IF and IOPL");
    for (unsigned i = 0; i < 7; ++i)
        check(cpu_test_observed[i] == expected_registers[i], "IRET restores general registers");
    check(cpu_test_observed[7] == (uintptr_t)cpu_test_user_stack_top, "IRET restores user ESP");
    for (unsigned i = 8; i < 13; ++i)
        check(cpu_test_observed[i] == USER_DATA_SELECTOR, "IRET restores data and stack segments");
    const uint32_t vectors[] = {6, 7, 6, 6, 13};
    const char *const instructions[] = {cpu_test_done, cpu_test_x87, cpu_test_mmx, cpu_test_sse, cpu_test_io};
    serial_writestring("rum_cpu_fault vector/error/eip: ");
    report_hex(frame->vector); report_hex(frame->error); report_hex(frame->eip);
    serial_writestring("\n");
    /* QEMU's MMX decoder checks TS before EM, reporting #NM rather than #UD
       when both are set. Either fault rejects the instruction before state use. */
    bool expected_vector = frame->vector == vectors[RUM_CPU_CASE] ||
                           (RUM_CPU_CASE == 2 && frame->vector == 7);
    check(expected_vector && frame->error == 0 &&
          frame->eip == (uintptr_t)instructions[RUM_CPU_CASE], "expected hardware fault and instruction");
    check(exception_frame_esp(frame) == (uintptr_t)cpu_test_user_stack_top,
          "fault reports interrupted user ESP");
    terminal_writestring("rum user CPU entry and policy tests passed.\n");
    serial_writestring("rum_cpu_test_ok\n");
    cpu_halt();
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    check(magic == 0x2BADB002 && information, "Multiboot handoff");
    uint32_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0; mov %%cr4, %1" : "=r"(cr0), "=r"(cr4));
    check((cr0 & CPU_INTEGER_CR0) == CPU_INTEGER_CR0 && !(cr4 & CPU_UNSUPPORTED_CR4),
          "integer-only hardware controls");
    uint32_t previous = rum_tss.esp0;
    check(!gdt_set_kernel_stack(0) && !gdt_set_kernel_stack((uintptr_t)cpu_test_stack_top - 1) &&
          rum_tss.esp0 == previous, "invalid TSS stack update has no effect");
    check(gdt_set_kernel_stack((uintptr_t)cpu_test_stack_top) && rum_tss.ss0 == KERNEL_DATA_SELECTOR,
          "install dedicated kernel entry stack");
    pic_initialize();
    timer_initialize();
    pic_unmask(0);
    struct exception_user_frame user;
    cpu_user_frame_initialize(&user, (uintptr_t)cpu_test_user, (uintptr_t)cpu_test_user_stack_top);
    user.core.eax = expected_registers[0]; user.core.ebx = expected_registers[1];
    user.core.ecx = expected_registers[2]; user.core.edx = expected_registers[3];
    user.core.esi = expected_registers[4]; user.core.edi = expected_registers[5];
    user.core.ebp = expected_registers[6];
    cpu_test_enter(&user);
}
