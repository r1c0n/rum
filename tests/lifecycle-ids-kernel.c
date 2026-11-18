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
static unsigned claim_stage;
static void check_saved_stack(task_id id, struct paging_space *space)
{
    struct rum_vm_allocate_request allocation = { .version = RUM_VM_ABI_VERSION, .size = sizeof allocation,
        .bytes = RUM_PAGE_SIZE, .protection = RUM_VM_READ | RUM_VM_WRITE };
    check(!paging_region_allocate(space, &allocation), "allocate alternate stack");
    struct rum_vm_protect_request protect = { .version = RUM_VM_ABI_VERSION, .size = sizeof protect,
        .address = allocation.address, .bytes = allocation.bytes, .protection = RUM_VM_READ };
    struct rum_vm_release_request release = { .version = RUM_VM_ABI_VERSION, .size = sizeof release,
        .address = allocation.address, .bytes = allocation.bytes };
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].id == id) {
        /* Seed the saved context of a runnable user task between dispatches. */
        uint32_t saved = tasks[i].user_frame.esp;
        tasks[i].user_frame.esp = allocation.address + allocation.bytes;
        check(paging_region_release(space, &release, 0) == -RUM_EACCES &&
              paging_region_protect(space, &protect, 0) == -RUM_EACCES &&
              paging_user_accessible(space, allocation.address, allocation.bytes, true), "peer retains its active stack");
        tasks[i].user_frame.esp = saved;
        check(!paging_region_release(space, &release, 0), "release unused alternate stack");
        return;
    }
    check(false, "find runnable stack owner");
}
static void competing_wait(void *argument)
{
    task_id child = *(task_id *)argument;
    struct task_information info;
    /* Run a second kernel continuation against the same parent container.
       Restore its private container before the ordinary worker exit path. */
    struct process_state *private_owner = current->process;
    current->process = tasks[BOOT].process;
    check(task_claim_child(RUM_ABI_PID_MAX - 1, true, &info) == 0 &&
          !task_collect_child(child), "a second waiter cannot steal a claimed result");
    claim_stage = 1;
    check(task_claim_child(RUM_ABI_PID_MAX - 1, false, &info) == 1 && info.id == child &&
          task_collect_child(child), "releasing a claim wakes the other parent continuation");
    current->process = private_owner;
    claim_stage = 2;
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
    check_saved_stack(a, first.space);
    rejected();
    /* Run both to completion before waiting: results must remain owned until
       collection, and an any-child wait skips no retained completion. */
    do { check(task_yield() && task_query(b, &info), "run boundary processes"); }
    while (info.state != TASK_EXITED);
    check(task_claim_child(RUM_ABI_PID_MAX - 1, true, &info) == 1 && info.id == a &&
          info.termination == TASK_TERMINATION_FAULT && task_create(competing_wait, &a, NULL),
          "claim completed child before running a competing waiter");
    while (!claim_stage) check(task_yield(), "run competing waiter");
    task_unclaim_child(a);
    while (claim_stage != 2) check(task_yield(), "run awakened waiter");
    (void)reap_exited(false); /* Retain the second child until the any-child wait. */
    check(task_wait_child(RUM_ABI_PID_MAX - 1, true, &info) == -RUM_ECHILD &&
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
