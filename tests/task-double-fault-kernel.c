/* Overflow a real guarded worker stack and require the hardware task gate. */
#include <rum/cpu.h>
#include <rum/diagnostics.h>
#include <rum/interrupts.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>

void kernel_main(uint32_t magic, uint32_t information);
_Noreturn void task_trigger_stack_guard(void);
extern const char __kernel_start[], __kernel_end[];
extern volatile uint32_t task_emergency_stack_base;
uint32_t task_double_fault_expected[10];

static void check(bool condition)
{
    if (!condition) {
        serial_writestring("rum_task_test_failed: double-fault setup\n");
        cpu_halt();
    }
}

static void worker(void *argument)
{
    (void)argument;
    struct kernel_diagnostics snapshot;
    struct task_information info;
    check(diagnostics_capture(&snapshot) && task_query(task_current_id(), &info));
    const uint32_t expected[] = {
        info.id, info.stack_slot, info.stack_base, info.stack_top,
        task_emergency_stack_base, snapshot.paging.kernel_directory,
        snapshot.cr3, snapshot.physical.free_pages,
        snapshot.tasks.stack_pages, snapshot.tasks.emergency_stack_pages,
    };
    for (unsigned i = 0; i < 10; ++i) task_double_fault_expected[i] = expected[i];
    serial_writestring("rum_double_fault_test_ready\n");
    task_trigger_stack_guard();
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() &&
          heap_initialize() && task_initialize());
    cpu_interrupt_enable();
    struct paging_space *space = paging_space_create();
    check(space && task_create(worker, NULL, space) && task_yield());
    check(false);
    cpu_halt();
}
