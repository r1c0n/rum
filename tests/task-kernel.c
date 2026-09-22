#include <rum/cpu.h>
#include <rum/diagnostics.h>
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
extern volatile uint32_t task_emergency_stack_base;
static struct task_event done, timed;
static volatile uint32_t finished, irq_checks, irq_target;
static uint32_t worker_runs[2];
static task_id workers[2];
static struct task_event broadcast;
static volatile uint32_t waiters_ready, waiters_done, idle_irqs;
static uint32_t short_runs;
static bool require_idle_ticks;
static task_id process_task;
static uint32_t process_directory;
static struct exception_user_frame process_frame;

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
    struct task_snapshot snapshot;
    uint32_t before = pmm_stats().free_pages;
    check(task_snapshot_read(&snapshot) && snapshot.current == info.id &&
          snapshot.states[TASK_RUNNING] == 1, "atomic current-task snapshot");
    bool found = false;
    for (uint32_t i = 0; i < snapshot.count; ++i) {
        const struct task_information *item = &snapshot.tasks[i];
        if (item->id == info.id) found = item->directory == cr3 && item->stack_top == rum_tss.esp0;
        if (item->owns_stack) {
            check(!paging_translate(paging_kernel_space(), RUM_KERNEL_STACK_GUARD(item->stack_slot), NULL),
                  "snapshot stack guard remains absent");
            for (uint32_t address = item->stack_base; address < item->stack_top; address += RUM_PAGE_SIZE) {
                uint32_t frame;
                check(paging_translate(paging_kernel_space(), address, &frame) && pmm_is_allocated(frame),
                      "snapshot stack frames remain owned");
            }
        }
        if (item->owns_space) check(pmm_is_allocated(item->directory), "snapshot directory remains owned");
    }
    struct paging_statistics paging = paging_stats();
    check(found && paging.active_directory == cr3 && paging.directory_pages == paging.spaces &&
          pmm_stats().free_pages == before, "snapshot hardware/ownership and no allocations");
    struct kernel_diagnostics diagnostics;
    check(diagnostics_capture(&diagnostics) && diagnostics.tasks.current == info.id &&
          diagnostics.cr3 == cr3 && diagnostics.esp0 == info.stack_top &&
          diagnostics.kernel_esp >= info.stack_base && diagnostics.kernel_esp < info.stack_top &&
          diagnostics.physical.free_pages == before && pmm_stats().free_pages == before &&
          (diagnostics.flags & 0x600) == 0x200, "complete diagnostics preserve context without allocation");
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    check((flags & 0x600) == 0x200, "diagnostics restore IF/DF before output");
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
    uint32_t frame;
    check(paging_translate(paging_kernel_space(), rum_tss.esp0 - RUM_KERNEL_STACK_SIZE, &frame) &&
          pmm_is_allocated(frame) && task_reap() == 0, "running stack cannot be reaped");
    ++finished;
    task_event_signal(&done);
    /* Returning from a C entry exits through the scheduler, never a freed stack. */
}

static void tick(void)
{
    check(irq_in_handler(), "IRQ context marker");
    struct task_snapshot snapshot;
    check(task_snapshot_read(&snapshot) && snapshot.current == task_current_id(), "IRQ-safe snapshot");
    uint32_t before = pmm_stats().free_pages;
    check(!task_yield() && !task_wait(&timed, timed.sequence) &&
          !task_create(task_test_entry, NULL, NULL) && task_reap() == 0,
          "IRQ cannot allocate, block, switch or reap tasks");
    check(pmm_stats().free_pages == before, "IRQ owns no task allocation");
    ++irq_checks;
    if (task_current_id() == 2) ++idle_irqs;
    if (irq_target && (require_idle_ticks ? idle_irqs >= 3 : irq_checks >= irq_target)) {
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

static uint32_t consume_until(uint32_t remaining)
{
    uint32_t head = 0, page;
    while (pmm_stats().free_pages > remaining && (page = pmm_allocate_page())) {
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

static __attribute__((noinline)) void allocation_boundaries(uint32_t baseline)
{
    /* Repeat each insufficient guarded-stack budget. Failed creation cannot
       publish a task or claim the caller's private directory/metadata. */
    for (unsigned round = 0; round < 3; ++round) {
        for (uint32_t remaining = 0; remaining < RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE; ++remaining) {
            struct paging_space *candidate = paging_space_create();
            uint32_t directory = paging_directory_address(candidate);
            check(candidate && directory, "allocation-boundary directory");
            struct heap_statistics heap = heap_stats();
            struct task_snapshot before, after;
            check(task_snapshot_read(&before), "allocation-boundary initial owners");
            uint32_t held = consume_until(remaining);
            check(!task_create(short_worker, NULL, candidate) &&
                  pmm_stats().free_pages == remaining && paging_directory_address(candidate) == directory &&
                  heap_stats().used_bytes == heap.used_bytes && heap_stats().allocations == heap.allocations,
                  "insufficient stack budget retains caller resources");
            check(task_snapshot_read(&after) && after.count == before.count &&
                  after.stack_pages == before.stack_pages && after.directory_pages == before.directory_pages &&
                  after.created == before.created && after.exited == before.exited && after.reaped == before.reaped,
                  "failed creation leaves task ownership/lifecycle unchanged");
            release_pages(held);
            check(paging_space_destroy(candidate) && pmm_stats().free_pages == baseline,
                  "repeated allocation failure returns all private frames");
        }
    }
    struct paging_space *candidate = paging_space_create();
    check(candidate, "exact stack-budget directory");
    uint32_t directory = paging_directory_address(candidate);
    uint32_t held = consume_until(RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE);
    task_id worker = task_create(short_worker, NULL, candidate);
    check(worker && pmm_stats().free_pages == 0, "exact four-page budget publishes worker");
    struct task_information info;
    check(task_query(worker, &info) && info.stack_slot == 1 &&
          info.stack_base == RUM_KERNEL_STACK_SLOT_BASE(1) && info.directory == directory &&
          info.owns_stack && info.owns_space, "successful creation transfers stack/directory ownership");
    while (task_query(worker, &info)) check(task_yield(), "execute exact-budget worker");
    (void)task_reap();
    check(pmm_stats().free_pages == 5 && !paging_directory_address(candidate),
          "worker reaper releases four stack pages plus private directory");
    release_pages(held);
    check(pmm_stats().free_pages == baseline, "exact stack budget leaves no private frames");
}

static __attribute__((noinline)) void process_records(uint32_t baseline)
{
    struct task_snapshot initial, before, after;
    check(task_snapshot_read(&initial) && !initial.processes && !initial.user_pages &&
          !initial.user_table_pages, "initial process ownership ledger");
    struct paging_space *space = paging_space_create();
    check(space && paging_user_allocate(space, RUM_USER_BASE, 1, PAGING_WRITABLE) &&
          paging_user_allocate(space, RUM_USER_STACK_TOP - RUM_PAGE_SIZE, 1, PAGING_WRITABLE),
          "prepare process mappings");
    const uint8_t instructions[] = {0x0F, 0x0B}; /* ud2 */
    const uint32_t user_esp = RUM_USER_STACK_TOP - 32;
    const uint32_t words[] = {1, user_esp + 16, 0, 0};
    const char name[] = "probe";
    check(paging_copy_to_user(space, RUM_USER_BASE, instructions, sizeof instructions) &&
          paging_copy_to_user(space, user_esp, words, sizeof words) &&
          paging_copy_to_user(space, user_esp + sizeof words, name, sizeof name) &&
          paging_user_protect(space, RUM_USER_BASE, 1, 0), "complete process image and arguments");
    struct paging_space_statistics ownership;
    process_directory = paging_directory_address(space);
    check(paging_space_stats(space, &ownership) && !ownership.kernel &&
          ownership.directory == process_directory && ownership.private_table_pages == 2 &&
          ownership.user_pages == 2, "per-space process ownership");

    process_frame = (struct exception_user_frame){
        .core = { .gs = USER_DATA_SELECTOR, .fs = USER_DATA_SELECTOR,
                  .es = USER_DATA_SELECTOR, .ds = USER_DATA_SELECTOR,
                  .eip = RUM_USER_BASE, .cs = USER_CODE_SELECTOR, .eflags = 0x202 },
        .esp = user_esp, .ss = USER_DATA_SELECTOR,
    };
    struct task_process process = { .space = space, .user_frame = process_frame };
    uint32_t prepared_free = pmm_stats().free_pages;
    check(task_snapshot_read(&before), "process validation baseline");
    struct task_process invalid = process;
    invalid.space = paging_kernel_space();
    check(!task_create_process(NULL) && !task_create_process(&invalid),
          "reject missing and kernel process definitions");
    invalid = process; invalid.user_frame.core.cs = KERNEL_CODE_SELECTOR;
    check(!task_create_process(&invalid), "reject untrusted process selectors");
    invalid = process; invalid.user_frame.core.eflags ^= 0x400;
    check(!task_create_process(&invalid), "reject unsafe process flags");
    invalid = process; invalid.user_frame.core.eip += RUM_PAGE_SIZE;
    check(!task_create_process(&invalid), "reject unmapped process entry");
    invalid = process; invalid.user_frame.esp = RUM_USER_STACK_BASE;
    check(!task_create_process(&invalid), "reject unmapped process stack");
    check(task_snapshot_read(&after) && after.count == before.count &&
          after.processes == before.processes && after.created == before.created &&
          pmm_stats().free_pages == prepared_free,
          "invalid process definitions remain private");

    for (uint32_t remaining = 0; remaining < RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE; ++remaining) {
        uint32_t held = consume_until(remaining);
        check(task_snapshot_read(&before), "snapshot before partial process stack");
        check(!task_create_process(&process), "partial process stack cannot publish");
        check(pmm_stats().free_pages == remaining, "partial process stack restores pages");
        check(paging_directory_address(space) == process_directory,
              "partial process stack retains caller address space");
        check(task_snapshot_read(&after) && after.count == before.count &&
              after.processes == before.processes && after.stack_pages == before.stack_pages &&
              after.directory_pages == before.directory_pages && after.user_pages == before.user_pages &&
              after.created == before.created, "failed process is never published");
        release_pages(held);
        check(pmm_stats().free_pages == prepared_free, "process stack rollback restores ledger");
    }

    uint32_t held = consume_until(RUM_KERNEL_STACK_SIZE / RUM_PAGE_SIZE);
    process_task = task_create_process(&process);
    check(process_task && pmm_stats().free_pages == 0, "exact process stack budget publishes once complete");
    /* Mutating the producer's packet cannot change the trusted record. */
    process.user_frame.core.eip = 0;
    struct task_information info;
    check(task_query(process_task, &info) && info.kind == TASK_PROCESS && info.process_id == 1 &&
          info.parent == 1 && info.state == TASK_RUNNABLE &&
          !memcmp(&info.user_frame, &process_frame, sizeof process_frame) &&
          info.termination == TASK_TERMINATION_NONE &&
          info.resources.kernel_stack_pages == 4 && info.resources.directory_pages == 1 &&
          info.resources.user_table_pages == 2 && info.resources.user_pages == 2,
          "published process record owns immutable state");
    process.user_frame = process_frame;
    check(!task_create_process(&process) && !task_create(short_worker, NULL, space) &&
          pmm_stats().free_pages == 0, "address space has one task owner");
    check(task_yield() && task_query(process_task, &info) && info.state == TASK_EXITED &&
          info.termination == TASK_TERMINATION_FAULT && info.process_id == 1 &&
          info.fault.vector == 6 && !info.fault.error && !info.fault.address &&
          info.fault.instruction == RUM_USER_BASE && info.fault.stack == user_esp &&
          info.user_frame.core.vector == 6 && info.user_frame.core.eip == RUM_USER_BASE,
          "ring-3 fault terminates only the process with a recorded reason");
    check(task_snapshot_read(&after) && after.processes == 1 &&
          after.states[TASK_EXITED] == 1 && after.user_table_pages == 2 && after.user_pages == 2 &&
          pmm_stats().free_pages == 0, "process resources remain until explicit reap");
    check(task_reap() == 1 && !task_query(process_task, &info) &&
          !paging_directory_address(space) && pmm_stats().free_pages == 9,
          "process reap releases stack and complete address space");
    ownership = (struct paging_space_statistics){ .directory = UINT32_MAX };
    check(!paging_space_stats(space, &ownership) && !ownership.directory,
          "destroyed process space has no stale statistics");
    release_pages(held);
    check(pmm_stats().free_pages == baseline, "process construction and exit leave no physical owners");
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize(); /* Mask/remap BIOS IRQs before enabling IF for task tests. */
    struct task_snapshot snapshot;
    memset(&snapshot, 0xFF, sizeof snapshot);
    check(!task_snapshot_read(NULL) && !task_snapshot_read(&snapshot) && !snapshot.count &&
          !snapshot.current && !paging_stats().spaces, "uninitialized snapshots fail without stale output");
    check(!task_current_id() && !task_yield() && !task_create(task_test_entry, NULL, NULL),
          "no tasks before initialization");
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    check(pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize(), "memory startup");
    uint32_t initial = pmm_stats().free_pages;
    for (uint32_t remaining = 0; remaining < 9; ++remaining) {
        uint32_t consumed = consume_until(remaining);
        check(!task_initialize() && pmm_stats().free_pages == remaining && !task_current_id(),
              "idle/emergency stack OOM rollback");
        release_pages(consumed);
        check(pmm_stats().free_pages == initial, "failed task initialization returns every frame");
    }
    check(task_initialize(), "task initialization retry");
    check(!task_initialize() && task_current_id() == 1, "one-time initialization and boot task");
    struct task_information info;
    check(task_query(2, &info) && info.stack_top - info.stack_base == RUM_KERNEL_STACK_SIZE,
          "private idle stack");
    check(task_emergency_stack_base == RUM_KERNEL_STACK_SLOT_BASE(RUM_KERNEL_STACK_SLOTS - 1) &&
          !paging_translate(paging_kernel_space(),
                            RUM_KERNEL_STACK_GUARD(RUM_KERNEL_STACK_SLOTS - 1), NULL),
          "dedicated guarded emergency stack");
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

    allocation_boundaries(baseline);
    process_records(baseline);

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
    check(short_runs == 8 * RUM_PROCESS_LIMIT + 1, "all bounded and exact-budget tasks executed");

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
    require_idle_ticks = true;
    irq_target = irq_checks + 3;
    check(task_wait(&timed, sequence), "idle context wakes from real PIT IRQ");
    check(irq_checks >= 3 && task_current_id() == 1, "idle wake restores boot context");
    check(idle_irqs >= 3, "PIT actually entered the dedicated idle context");
    context_check();
    irq_target = 0;
    require_idle_ticks = false;

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
    struct paging_space *candidate = paging_space_create();
    task_id exiting = task_create(short_worker, NULL, candidate);
    check(candidate && exiting, "exit-to-idle task with private CR3");
    sequence = task_event_sequence(&timed);
    irq_target = irq_checks + 3;
    check(task_wait(&timed, sequence), "idle survives owned-context exit");
    irq_target = 0;
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && snapshot.stack_pages == 4 &&
          snapshot.emergency_stack_pages == 4 &&
          !snapshot.directory_pages && snapshot.created == snapshot.exited &&
          snapshot.created == snapshot.reaped && snapshot.switches > snapshot.created &&
          paging_stats().directory_pages == 1, "final ownership and lifecycle ledger");
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
