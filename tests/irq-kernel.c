#include <rum/cpu.h>
#include <rum/interrupts.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>

void kernel_main(uint32_t magic, uint32_t information);
void irq_probe(void);
extern const uint32_t irq_observed[8], irq_expected_esp, irq_observed_flags;

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    if (magic != 0x2BADB002 || information == 0)
        cpu_halt();
    irq_probe();
    const uint32_t expected[8] = {0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00,
                                 0x13579BDF, 0x2468ACE0, 0x0BADF00D, irq_expected_esp};
    for (unsigned i = 0; i < 8; ++i) {
        if (irq_observed[i] != expected[i]) {
            serial_writestring("rum_irq_test_bad_register\n");
            cpu_halt();
        }
    }
    if ((irq_observed_flags & 0x600) != 0x400 || irq_spurious_count() != 2) {
        serial_writestring("rum_irq_test_bad_flags_or_spurious_count\n");
        cpu_halt();
    }
    terminal_writestring("rum IRQ return and spurious interrupt tests passed.\n");
    serial_writestring("rum_irq_test_ok\n");
    cpu_halt();
}
