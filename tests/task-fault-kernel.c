/* A real kernel fault while a worker owns its active stack and CR3. */
#include <rum/cpu.h>
#include <rum/diagnostics.h>
#include <rum/interrupts.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>

void kernel_main(uint32_t magic, uint32_t information);
_Noreturn void fault_trigger_ud(void);
extern const char __kernel_start[], __kernel_end[];
/* Independent monitor expectations captured before entering the panic path. */
uint32_t task_fault_expected[12];

static void check(bool condition)
{
    if (!condition) {
        serial_writestring("rum_task_test_failed: owned-context fault setup\n");
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
        info.id, snapshot.cr3, snapshot.esp0, info.stack_base, info.stack_top,
        snapshot.physical.free_pages, snapshot.physical.managed_pages,
        snapshot.paging.directory_pages, snapshot.paging.shared_table_pages,
        snapshot.tasks.stack_pages, snapshot.heap.allocations, snapshot.heap.used_bytes,
    };
    for (unsigned i = 0; i < 12; ++i) task_fault_expected[i] = expected[i];
    fault_trigger_ud();
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize() && task_initialize());
    cpu_interrupt_enable();
    struct paging_space *space = paging_space_create();
    check(space && task_create(worker, NULL, space) && task_yield());
    check(false);
    cpu_halt();
}
