#include <rum/cpu.h>
#include <rum/cpu_policy.h>
#include <rum/gdt.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/io.h>
#include <rum/keyboard.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/pmm.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>

void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[], __boot_stack_top[];

#define DECLARE_PROBE(name) \
    extern const uint8_t process_fault_##name##_start[]; \
    extern const uint8_t process_fault_##name##_instruction[]; \
    extern const uint8_t process_fault_##name##_end[]

DECLARE_PROBE(null);
DECLARE_PROBE(kernel);
DECLARE_PROBE(readonly);
DECLARE_PROBE(ud2);
DECLARE_PROBE(privileged);
DECLARE_PROBE(io);
DECLARE_PROBE(irq);

#if RUM_PROCESS_FAULT_CASE == 0
#define PROBE_START process_fault_null_start
#define PROBE_INSTRUCTION process_fault_null_instruction
#define PROBE_END process_fault_null_end
#define EXPECTED_VECTOR 14u
#define EXPECTED_ERROR 4u
#define EXPECTED_ADDRESS 0u
#elif RUM_PROCESS_FAULT_CASE == 1
#define PROBE_START process_fault_kernel_start
#define PROBE_INSTRUCTION process_fault_kernel_instruction
#define PROBE_END process_fault_kernel_end
#define EXPECTED_VECTOR 14u
#define EXPECTED_ERROR 5u
#define EXPECTED_ADDRESS RUM_KERNEL_LOAD_BASE
#elif RUM_PROCESS_FAULT_CASE == 2
#define PROBE_START process_fault_readonly_start
#define PROBE_INSTRUCTION process_fault_readonly_instruction
#define PROBE_END process_fault_readonly_end
#define EXPECTED_VECTOR 14u
#define EXPECTED_ERROR 7u
#define EXPECTED_ADDRESS RUM_USER_BASE
#elif RUM_PROCESS_FAULT_CASE == 3
#define PROBE_START process_fault_ud2_start
#define PROBE_INSTRUCTION process_fault_ud2_instruction
#define PROBE_END process_fault_ud2_end
#define EXPECTED_VECTOR 6u
#define EXPECTED_ERROR 0u
#define EXPECTED_ADDRESS 0u
#elif RUM_PROCESS_FAULT_CASE == 4
#define PROBE_START process_fault_privileged_start
#define PROBE_INSTRUCTION process_fault_privileged_instruction
#define PROBE_END process_fault_privileged_end
#define EXPECTED_VECTOR 13u
#define EXPECTED_ERROR 0u
#define EXPECTED_ADDRESS 0u
#elif RUM_PROCESS_FAULT_CASE == 5
#define PROBE_START process_fault_io_start
#define PROBE_INSTRUCTION process_fault_io_instruction
#define PROBE_END process_fault_io_end
#define EXPECTED_VECTOR 13u
#define EXPECTED_ERROR 0u
#define EXPECTED_ADDRESS 0u
#elif RUM_PROCESS_FAULT_CASE == 6
#define PROBE_START process_fault_irq_start
#define PROBE_INSTRUCTION process_fault_irq_instruction
#define PROBE_END process_fault_irq_end
#define EXPECTED_VECTOR 6u
#define EXPECTED_ERROR 0u
#define EXPECTED_ADDRESS 0u
#else
#error "Unknown process fault case"
#endif

#define USER_DATA (RUM_USER_BASE + RUM_PAGE_SIZE)

static volatile uint32_t process_timer_irqs, process_keyboard_irqs, survivor_runs;

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_process_fault_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static void report_hex(uint32_t number)
{
    static const char digits[] = "0123456789abcdef";
    char text[11] = "0x00000000";
    for (unsigned i = 0; i < 8; ++i)
        text[2 + i] = digits[(number >> (28 - i * 4)) & 15];
    serial_writestring(text);
}

static void survivor(void *argument)
{
    (void)argument;
    ++survivor_runs;
}

#if RUM_PROCESS_FAULT_CASE == 6
static void process_timer_interrupt(void)
{
    if (task_current_is_process()) {
        ++process_timer_irqs;
        *(volatile uint32_t *)(uintptr_t)USER_DATA = process_timer_irqs;
    }
}

static void process_keyboard_interrupt(void)
{
    uint8_t status = inb(0x64);
    if (status & 1) {
        (void)inb(0x60);
        if (task_current_is_process()) {
            ++process_keyboard_irqs;
            *(volatile uint32_t *)(uintptr_t)(USER_DATA + 4) = process_keyboard_irqs;
        }
    }
}
#endif

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    check(pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize() &&
          task_initialize(), "memory and task startup");
    uint32_t baseline = pmm_stats().free_pages;

    struct paging_space *space = paging_space_create();
    uint32_t user_pages = RUM_PROCESS_FAULT_CASE == 6 ? 2 : 1;
    check(space && paging_user_allocate(space, RUM_USER_BASE, user_pages, PAGING_WRITABLE) &&
          paging_user_allocate(space, RUM_USER_STACK_TOP - RUM_PAGE_SIZE, 1, PAGING_WRITABLE),
          "private process mappings");
    size_t code_bytes = (size_t)(PROBE_END - PROBE_START);
    check(code_bytes && code_bytes <= RUM_PAGE_SIZE &&
          paging_copy_to_user(space, RUM_USER_BASE, PROBE_START, code_bytes) &&
          paging_user_protect(space, RUM_USER_BASE, 1, 0), "copy read-only user probe");

    uint32_t user_esp = RUM_USER_STACK_TOP - 32;
    const uint32_t initial_stack[] = {0, 0, 0, 0};
    check(paging_copy_to_user(space, user_esp, initial_stack, sizeof initial_stack),
          "initialize user stack");
    struct exception_user_frame frame;
    cpu_user_frame_initialize(&frame, RUM_USER_BASE, user_esp);
    cpu_interrupt_enable();
    task_id process = task_create_process(&(struct task_process){
        .space = space,
        .user_frame = frame,
    });
    check(process, "publish process");

#if RUM_PROCESS_FAULT_CASE == 6
    timer_initialize();
    check(keyboard_initialize(), "PS/2 setup");
    irq_register(0, process_timer_interrupt);
    irq_register(1, process_keyboard_interrupt);
    pic_unmask(0);
    pic_unmask(1);
    serial_writestring("rum_process_irq_ready\n");
#endif

    check(task_yield(), "run process");
    struct task_information info;
    uint32_t expected_eip = RUM_USER_BASE + (uint32_t)(PROBE_INSTRUCTION - PROBE_START);
    check(task_current_id() == 1 && paging_active_space() == paging_kernel_space() &&
          rum_tss.esp0 == (uintptr_t)__boot_stack_top, "parent CR3 and TSS restored");
    check(task_query(process, &info) && info.kind == TASK_PROCESS &&
          info.state == TASK_EXITED && info.termination == TASK_TERMINATION_FAULT,
          "faulted process remains observable");
    check(info.fault.vector == EXPECTED_VECTOR && info.fault.error == EXPECTED_ERROR &&
          info.fault.address == EXPECTED_ADDRESS && info.fault.instruction == expected_eip &&
          info.fault.stack == user_esp, "recorded hardware fault reason");
    check(info.user_frame.core.vector == EXPECTED_VECTOR &&
          info.user_frame.core.error == EXPECTED_ERROR && info.user_frame.core.eip == expected_eip &&
          info.user_frame.core.cs == USER_CODE_SELECTOR && info.user_frame.esp == user_esp &&
          info.user_frame.ss == USER_DATA_SELECTOR, "saved privilege-change frame");
    check(info.user_frame.core.ebx == 0x11223344 && info.user_frame.core.ecx == 0x55667788 &&
          info.user_frame.core.edx == 0x99AABBCC && info.user_frame.core.edi == 0x13579BDF &&
          info.user_frame.core.ebp == 0x2468ACE0, "saved user registers");
#if RUM_PROCESS_FAULT_CASE == 6
    check(process_timer_irqs >= 3 && process_keyboard_irqs >= 1,
          "timer and keyboard IRQs returned through ring 3");
#endif

    serial_writestring("rum_process_fault vector="); report_hex(info.fault.vector);
    serial_writestring(" error="); report_hex(info.fault.error);
    serial_writestring(" address="); report_hex(info.fault.address);
    serial_writestring(" eip="); report_hex(info.fault.instruction);
    serial_writestring(" esp="); report_hex(info.fault.stack);
    serial_writestring("\n");

    check(task_reap() == 1 && !task_query(process, &info) &&
          pmm_stats().free_pages == baseline, "complete process cleanup");
    task_id worker = task_create(survivor, NULL, NULL);
    check(worker && task_yield() && survivor_runs == 1 && !task_query(worker, &info) &&
          pmm_stats().free_pages == baseline, "parent scheduler remains usable");

    terminal_writestring("rum user fault recovery tests passed.\n");
    serial_writestring("rum_process_fault_test_ok\n");
    cpu_halt();
}
