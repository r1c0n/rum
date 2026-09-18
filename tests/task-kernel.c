#include <rum/cpu.h>
#include <rum/gdt.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/process_limits.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>

void kernel_main(uint32_t magic, uint32_t information);
void task_test_entry(void *argument);
void task_test_worker(void *argument);
bool task_test_registers(void);
extern const uint32_t task_test_entry_alignment;
extern const char __kernel_start[], __kernel_end[], __boot_stack_top[];
static struct task_event done, timed;
static volatile uint32_t finished, irq_checks, irq_target;
static uint32_t worker_runs[2];
static task_id workers[2];
static struct task_event broadcast;
static volatile uint32_t waiters_ready, waiters_done, idle_irqs;
static uint32_t short_runs;

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_task_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static void context_check(void)
{
    uint32_t flags, stack, cr3;
    __asm__ volatile ("pushfl; popl %0; mov %%esp, %1; mov %%cr3, %2"
                      : "=r"(flags), "=r"(stack), "=r"(cr3));
    struct task_information info;
    check(task_query(task_current_id(), &info) && info.state == TASK_RUNNING, "current task record");
    check(stack >= info.stack_base && stack < info.stack_top && rum_tss.esp0 == info.stack_top,
          "private active stack and TSS ESP0");
    check(cr3 == info.directory && paging_directory_address(paging_active_space()) == cr3,
          "current task CR3");
    check((flags & 0x600) == 0x200, "kernel context IF/DF");
}

void task_test_worker(void *argument)
{
    uintptr_t index = (uintptr_t)argument;
    check(index < 2 && task_current_id() == workers[index], "entry arguments and task identity");
    check(task_test_entry_alignment == 12, "C task entry alignment");
    context_check();
    for (unsigned i = 0; i < 8; ++i) {
        ++worker_runs[index];
        check(task_test_registers(), "callee-saved registers/ESP across switches");
        context_check();
    }
    uint32_t stack = rum_tss.esp0 - RUM_KERNEL_STACK_SIZE;
    check(pmm_is_allocated(stack) && task_reap() == 0, "running stack cannot be reaped");
    ++finished;
    task_event_signal(&done);
    /* Returning from a C entry exits through the scheduler, never a freed stack. */
}

static void tick(void)
{
    check(irq_in_handler(), "IRQ context marker");
    uint32_t before = pmm_stats().free_pages;
    check(!task_yield() && !task_wait(&timed, timed.sequence) &&
          !task_create(task_test_entry, NULL, NULL) && task_reap() == 0,
          "IRQ cannot allocate, block, switch or reap tasks");
    check(pmm_stats().free_pages == before, "IRQ owns no task allocation");
    ++irq_checks;
    if (task_current_id() == 2) ++idle_irqs;
    if (irq_target && irq_checks >= irq_target) {
        task_event_signal(&timed);
        task_event_signal(&broadcast);
    }
}

static void short_worker(void *argument)
{
    (void)argument;
    context_check();
    ++short_runs;
}

static void waiter(void *argument)
{
    (void)argument;
    uint32_t sequence = task_event_sequence(&broadcast);
    ++waiters_ready;
    check(task_wait(&broadcast, sequence), "attach broadcast waiter");
    context_check();
    ++waiters_done;
    task_event_signal(&done);
}

static uint32_t consume_pages(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) {
        *(uint32_t *)(uintptr_t)page = head;
        head = page;
    }
    return head;
}

static void release_pages(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "release exhaustion chain");
        head = next;
    }
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize(); /* Mask/remap BIOS IRQs before enabling IF for task tests. */
    check(!task_current_id() && !task_yield() && !task_create(task_test_entry, NULL, NULL),
          "no tasks before initialization");
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    check(pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize(), "memory startup");
    uint32_t initial = pmm_stats().free_pages;
    uint32_t consumed = consume_pages();
    check(!task_initialize() && pmm_stats().free_pages == 0 && !task_current_id(), "idle-stack OOM rollback");
    release_pages(consumed);
    check(pmm_stats().free_pages == initial && task_initialize(), "task initialization retry");
    check(!task_initialize() && task_current_id() == 1, "one-time initialization and boot task");
    struct task_information info;
    check(task_query(2, &info) && info.stack_top - info.stack_base == RUM_KERNEL_STACK_SIZE,
          "private idle stack");
    cpu_interrupt_enable();
    context_check();
    uint32_t baseline = pmm_stats().free_pages;
    struct paging_space *spaces[2] = {paging_space_create(), paging_space_create()};
    check(spaces[0] && spaces[1], "owned paging contexts");
    size_t heap_used = heap_stats().used_bytes;
    check(!task_create(NULL, NULL, spaces[0]) && !task_create(task_test_entry, NULL, paging_kernel_space()) &&
          !task_create(task_test_entry, NULL, (void *)0x12345), "invalid task setup");
    for (unsigned i = 0; i < 2; ++i) {
        workers[i] = task_create(task_test_entry, (void *)(uintptr_t)i, spaces[i]);
        check(workers[i] && !task_create(task_test_entry, NULL, spaces[i]), "exclusive space ownership");
    }
    while (finished != 2) {
        uint32_t sequence = task_event_sequence(&done);
        if (finished != 2) check(task_wait(&done, sequence), "wait for task completion");
    }
    (void)task_reap();
    check(worker_runs[0] == 8 && worker_runs[1] == 8 && !task_query(workers[0], &info) &&
          !task_query(workers[1], &info), "both contexts ran and exited");
    check(paging_active_space() == paging_kernel_space() && rum_tss.esp0 == (uintptr_t)__boot_stack_top,
          "surviving boot context");
    check(pmm_stats().free_pages == baseline && heap_stats().used_bytes < heap_used,
          "deferred stack/directory/metadata cleanup");

    /* Failed creation leaves an owned-space candidate with its caller. */
    struct paging_space *candidate = paging_space_create();
    check(candidate, "failure fixture directory");
    uint32_t candidate_directory = paging_directory_address(candidate);
    consumed = consume_pages();
    check(!task_create(short_worker, NULL, candidate) && pmm_stats().free_pages == 0 &&
          paging_directory_address(candidate) == candidate_directory, "stack OOM retains caller directory");
    release_pages(consumed);
    check(paging_space_destroy(candidate) && pmm_stats().free_pages == baseline,
          "caller cleanup after failed task setup");

    /* Exercise the bounded registry repeatedly, including borrowed kernel spaces.
       Old IDs must never refer to a later occupant of the same slot. */
    task_id previous_id = 0;
    for (unsigned round = 0; round < 8; ++round) {
        task_id ids[RUM_PROCESS_LIMIT];
        for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) {
            ids[i] = task_create(short_worker, NULL, NULL);
            check(ids[i] && ids[i] != previous_id && !task_query(previous_id, &info), "fresh task IDs");
        }
        uint32_t free = pmm_stats().free_pages;
        check(!task_create(short_worker, NULL, NULL) && pmm_stats().free_pages == free,
              "task limit allocates no additional stack");
        while (task_query(ids[RUM_PROCESS_LIMIT - 1], &info)) check(task_yield(), "run bounded task group");
        (void)task_reap();
        previous_id = ids[0];
        check(!task_query(previous_id, &info) && pmm_stats().free_pages == baseline,
              "borrowed directory survives repeated stack cleanup");
    }
    check(short_runs == 8 * RUM_PROCESS_LIMIT, "all bounded tasks executed");

    /* Signal between the predicate check and sleep: wait must return immediately. */
    uint32_t sequence = task_event_sequence(&timed);
    task_event_signal(&timed);
    check(task_wait(&timed, sequence) && task_current_id() == 1, "event before wait cannot be lost");
    timed.sequence = UINT32_MAX;
    task_event_signal(&timed);
    check(task_wait(&timed, UINT32_MAX) && !timed.sequence, "sequence wrap boundary");

    pic_initialize();
    timer_initialize();
    irq_register(0, tick);
    pic_unmask(0);
    sequence = task_event_sequence(&timed);
    irq_target = irq_checks + 3;
    check(task_wait(&timed, sequence), "idle context wakes from real PIT IRQ");
    check(irq_checks >= 3 && task_current_id() == 1, "idle wake restores boot context");
    check(idle_irqs >= 3, "PIT actually entered the dedicated idle context");
    context_check();
    irq_target = 0;

    task_id waiting_ids[3];
    for (unsigned i = 0; i < 3; ++i) {
        waiting_ids[i] = task_create(waiter, NULL, NULL);
        check(waiting_ids[i], "create broadcast waiters");
    }
    while (waiters_ready != 3) check(task_yield(), "run waiters to blocking boundary");
    for (unsigned i = 0; i < 3; ++i)
        check(task_query(waiting_ids[i], &info) && info.state == TASK_BLOCKED, "waiter state is blocked");
    sequence = task_event_sequence(&timed);
    irq_target = irq_checks + 3;
    check(task_wait(&timed, sequence), "IRQ wakes all broadcast waiters");
    irq_target = 0;
    while (waiters_done != 3) {
        sequence = task_event_sequence(&done);
        if (waiters_done != 3) check(task_wait(&done, sequence), "wait for resumed broadcast tasks");
    }
    (void)task_reap();
    check(pmm_stats().free_pages == baseline, "broadcast waiters leave no stacks");

    /* The worker exits while boot is blocked, forcing reclamation from idle
       rather than returning directly to the boot stack. */
    candidate = paging_space_create();
    task_id exiting = task_create(short_worker, NULL, candidate);
    check(candidate && exiting, "exit-to-idle task with private CR3");
    sequence = task_event_sequence(&timed);
    irq_target = irq_checks + 3;
    check(task_wait(&timed, sequence), "idle survives owned-context exit");
    irq_target = 0;
    check(!task_query(exiting, &info) && pmm_stats().free_pages == baseline,
          "idle reclaims inactive stack and private address space");
    for (unsigned i = 0; i < 32; ++i) {
        sequence = task_event_sequence(&timed);
        irq_target = irq_checks + 1;
        check(task_wait(&timed, sequence), "repeated IRQ at sleep boundary");
        context_check();
    }
    irq_target = 0;
    (void)cpu_interrupt_save();
    check(!task_wait(&timed, timed.sequence) && !task_yield() &&
          !task_create(task_test_entry, NULL, NULL), "IF-clear callers cannot suspend or create tasks");
    terminal_writestring("rum kernel contexts and waiting tests passed.\n");
    serial_writestring("rum_task_test_ok\n");
    cpu_halt();
}
