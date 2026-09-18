/* Install the IDT and report fatal CPU exceptions. */
#include <stdbool.h>
#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/serial.h>
#include <rum/terminal.h>

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t reserved;
    uint8_t attributes;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_gate) == 8, "32-bit IDT gate size");
_Static_assert(sizeof(struct idt_descriptor) == 6, "32-bit IDTR operand size");

static struct idt_gate idt[256] __attribute__((aligned(16)));
extern const uintptr_t exception_stub_table[32];
extern const uintptr_t irq_stub_table[16];
static bool panicking;

void idt_initialize(void)
{
    memset(idt, 0, sizeof(idt));
    for (unsigned vector = 0; vector < 48; ++vector) {
        uintptr_t address = vector < 32 ? exception_stub_table[vector] : irq_stub_table[vector - 32];
        idt[vector] = (struct idt_gate) {
            .offset_low = (uint16_t)address,
            .selector = KERNEL_CODE_SELECTOR,
            .reserved = 0,
            .attributes = 0x8E, /* Present, ring 0, 32-bit interrupt gate. */
            .offset_high = (uint16_t)(address >> 16),
        };
    }
    const struct idt_descriptor descriptor = {
        .limit = sizeof(idt) - 1,
        .base = (uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(descriptor) : "memory");
}

static void write(const char *text)
{
    terminal_writestring(text);
    serial_writestring(text);
}

static void value(const char *name, uint32_t number)
{
    static const char digits[] = "0123456789abcdef";
    char text[11] = "0x00000000";
    for (unsigned i = 0; i < 8; ++i)
        text[2 + i] = digits[(number >> (28 - i * 4)) & 0xF];
    write(name);
    write("=");
    write(text);
}

static const char *const exception_names[32] = {
    "Divide error", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device unavailable",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating-point error", "Alignment check", "Machine check", "SIMD floating-point error",
    "Virtualization exception", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security exception", "Reserved",
};

_Noreturn void exception_dispatch(const struct exception_frame *frame)
{
    __asm__ volatile ("cli" : : : "memory");
    if (panicking)
        cpu_halt();
    panicking = true;
    /* Read CR2 before console output in case the original fault was #PF. */
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    terminal_initialize();
    terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
    write("\n  rum kernel panic\n\n  ");
    write(frame->vector < 32 ? exception_names[frame->vector] : "Unknown exception");
    write("\n\n  ");
    value("vector", frame->vector); write("  "); value("error", frame->error);
    write("\n  "); value("eip", frame->eip); write("  "); value("cs", frame->cs & 0xFFFF);
    write("\n  "); value("eflags", frame->eflags);
    write("\n\n  "); value("eax", frame->eax); write("  "); value("ebx", frame->ebx);
    write("\n  "); value("ecx", frame->ecx); write("  "); value("edx", frame->edx);
    write("\n  "); value("esi", frame->esi); write("  "); value("edi", frame->edi);
    write("\n  "); value("ebp", frame->ebp); write("  ");
    value("esp", exception_frame_esp(frame));
    if (exception_frame_from_user(frame)) {
        write("  "); value("ss", exception_frame_ss(frame));
    }
    write("\n  "); value("ds", frame->ds); write("  "); value("es", frame->es);
    write("\n  "); value("fs", frame->fs); write("  "); value("gs", frame->gs);
    if (frame->vector == 14) {
        write("\n\n  "); value("cr2", fault_address);
        write((frame->error & 1) ? "  protection violation" : "  page not present");
        write((frame->error & 2) ? ", write" : ", read");
        write((frame->error & 4) ? ", user" : ", supervisor");
    }
    write("\n\n  CPU halted. Close QEMU to return to your host.\n");
    serial_writestring("rum_panic_halted\n");
    cpu_halt();
}
