#include <rum/abi/error.h>
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/cpu_policy.h>
#include <rum/gdt.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
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

#define USER_DATA (RUM_USER_BASE + RUM_PAGE_SIZE)
#define CROSS_BUFFER (USER_DATA + RUM_PAGE_SIZE - 64)
#define ERROR_BUFFER (USER_DATA + 0x300)

void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[], __boot_stack_top[];
extern const uint8_t syscall_probe_start[], syscall_probe_end[];
static volatile uint32_t worker_runs;

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_syscall_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static void worker(void *argument)
{
    (void)argument;
    ++worker_runs;
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    timer_initialize();
    check(keyboard_initialize(), "PS/2 setup");
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    check(pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize() &&
          task_initialize(), "memory and task startup");
    uint32_t baseline = pmm_stats().free_pages;

    struct paging_space *space = paging_space_create();
    check(space && paging_user_allocate(space, RUM_USER_BASE, 3, PAGING_WRITABLE) &&
          paging_user_allocate(space, RUM_USER_STACK_TOP - RUM_PAGE_SIZE, 1, PAGING_WRITABLE),
          "private syscall mappings");
    size_t code_bytes = (size_t)(syscall_probe_end - syscall_probe_start);
    check(code_bytes && code_bytes <= RUM_PAGE_SIZE &&
          paging_copy_to_user(space, RUM_USER_BASE, syscall_probe_start, code_bytes),
          "copy syscall probe");
    char output[192];
    memset(output, 'W', sizeof output);
    check(paging_copy_to_user(space, CROSS_BUFFER, output, sizeof output) &&
          paging_copy_to_user(space, ERROR_BUFFER, "err", 3) &&
          paging_user_protect(space, RUM_USER_BASE, 1, 0), "prepare syscall data");

    uint32_t user_esp = RUM_USER_STACK_TOP - 32;
    const uint32_t stack[] = {0, 0, 0, 0};
    check(paging_copy_to_user(space, user_esp, stack, sizeof stack), "initialize user stack");
    struct exception_user_frame frame;
    cpu_user_frame_initialize(&frame, RUM_USER_BASE, user_esp);
    cpu_interrupt_enable();
    task_id process = task_create_process(&(struct task_process){
        .space = space,
        .user_frame = frame,
    });
    task_id other = task_create(worker, NULL, NULL);
    check(process && other, "publish syscall process and worker");
    pic_unmask(0);
    pic_unmask(1);
    uint32_t ticks = timer_ticks();
    serial_writestring("rum_syscall_test_ready\n");

    while (true) {
        struct task_information info;
        if (task_query(process, &info) && info.state == TASK_EXITED) break;
        uint32_t observed = task_event_sequence(task_work_event());
        if (task_query(process, &info) && info.state != TASK_EXITED)
            check(task_wait(task_work_event(), observed), "wait for syscall process");
    }

    struct task_information info;
    check(task_query(process, &info) && info.termination == TASK_TERMINATION_EXIT &&
          info.exit_status == -37 && info.process_id == 1, "signed process exit");
    check(task_current_id() == 1 && paging_active_space() == paging_kernel_space() &&
          rum_tss.esp0 == (uintptr_t)__boot_stack_top, "parent context restored");
    check(worker_runs == 1 && !task_query(other, &info), "other runnable task progressed");
    check((timer_ticks() - ticks) >= 3, "PIT progressed during blocking read");

    rum_result_t results[18];
    check(paging_copy_from_user(space, results, USER_DATA, sizeof results), "copy syscall results");
    check(results[0] == 1 && results[1] == 1, "getpid and register preservation");
    check(results[2] == -RUM_ENOSYS && results[3] == -RUM_EBADF &&
          results[4] == -RUM_EBADF, "unknown syscall and invalid handles");
    check(results[5] == 0 && results[6] == 0, "zero-length operations");
    check(results[7] == -RUM_EFAULT && results[8] == -RUM_EFAULT &&
          results[9] == -RUM_EFAULT && results[10] == -RUM_EFAULT &&
          results[11] == -RUM_EFAULT, "user range and permission validation");
    check(results[12] == 128 && results[13] == 1 && results[14] == 3,
          "partial cross-page writes and stderr");
    check(results[15] == 1 && results[16] == 'x' && results[17] == 0,
          "blocking keyboard read and non-returning exit");

    check(task_reap() == 1 && !task_query(process, &info) &&
          pmm_stats().free_pages == baseline, "syscall process cleanup");
    terminal_writestring("rum production syscall tests passed.\n");
    serial_writestring("rum_syscall_test_ok\n");
    cpu_halt();
}
