/* Isolated boundary fixture: include the production scheduler to seed private
   monotonic counters near exhaustion. No test setter is exported by rum. */
#include "../kernel/process/task.c"
#include <rum/cpu_policy.h>
#include <rum/heap.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>
#include <rum/timer.h>

void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
static void check(bool condition, const char *name)
{
    if (condition) return;
    serial_writestring("rum_lifecycle_ids_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static struct task_process image(void)
{
    struct task_process process = { .space = paging_space_create() };
    const uint8_t code[] = {0x0f, 0x0b};
    check(process.space && paging_user_allocate(process.space, RUM_USER_BASE, 1, PAGING_WRITABLE) &&
          paging_user_allocate(process.space, RUM_USER_STACK_TOP - RUM_PAGE_SIZE, 1, PAGING_WRITABLE) &&
          paging_copy_to_user(process.space, RUM_USER_BASE, code, sizeof code) &&
          paging_user_protect(process.space, RUM_USER_BASE, 1, 0), "prepare boundary process");
    cpu_user_frame_initialize(&process.user_frame, RUM_USER_BASE, RUM_USER_STACK_TOP - 32);
    return process;
}
static void rejected(void)
{
    struct task_process candidate = image();
    uint32_t pages = pmm_stats().free_pages, published = created;
    check(task_process_creation_status(false) == -RUM_EOVERFLOW &&
          !task_create_process(&candidate) && created == published && pmm_stats().free_pages == pages &&
          paging_space_destroy(candidate.space), "exhausted IDs never publish or take caller resources");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize() &&
          task_initialize(), "boundary fixture startup");
    cpu_interrupt_enable(); pic_unmask(0);
    uint32_t pages = pmm_stats().free_pages;
    struct heap_statistics heap = heap_stats();
    next_process_id = RUM_ABI_PID_MAX - 1;
    struct task_process first = image(), second = image();
    task_id a = task_create_process(&first), b = task_create_process(&second);
    struct task_information info;
    check(a && b && task_query(a, &info) && info.process_id == RUM_ABI_PID_MAX - 1 &&
          task_query(b, &info) && info.process_id == RUM_ABI_PID_MAX, "last positive PIDs publish exactly once");
    rejected();
    /* Run both to completion before waiting: results must remain owned until
       collection, and an any-child wait skips no retained completion. */
    do { check(task_yield() && task_query(b, &info), "run boundary processes"); }
    while (info.state != TASK_EXITED);
    check(task_wait_child(RUM_ABI_PID_MAX - 1, true, &info) == 1 && info.id == a &&
          info.termination == TASK_TERMINATION_FAULT && task_collect_child(a) &&
          task_wait_child(RUM_ABI_PID_MAX - 1, true, &info) == -RUM_ECHILD &&
          task_wait_child(0, false, &info) == 1 && info.id == b && task_collect_child(b),
          "child exits before wait, any-child selection, and stale PID rejection");
    rejected(); /* Reaping cannot reopen the exhausted PID namespace. */
    /* Independent task-ID scenario in this fixture only. PIDs 1.. were never
       used above, so even this seeded scenario does not alias an earlier PID. */
    next_process_id = 1; next_id = UINT32_MAX;
    first = image(); a = task_create_process(&first);
    check(a == UINT32_MAX && !next_id, "last task identity does not wrap into BOOT or idle");
    rejected();
    check(task_wait_child(1, false, &info) == 1 && info.id == a && task_collect_child(a) &&
          task_wait_child(0, true, &info) == -RUM_ECHILD, "collect final task identity exactly once");
    check(created == exited && exited == reaped && heap_stats().allocations == heap.allocations &&
          heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "boundary failures and collection retain no resources");
    serial_writestring("rum_lifecycle_ids_ok\n"); cpu_halt();
}
