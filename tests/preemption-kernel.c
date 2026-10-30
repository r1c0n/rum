/* Isolated fixture: real private processes, PIT, IRQ return and scheduler.
   Only a test-only syscall and a nested IRQ are added to the normal dispatch. */
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/cpu_policy.h>
#include <rum/fs.h>
#include <rum/gdt.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>

void kernel_main(uint32_t magic, uint32_t information);
extern const uint8_t preemption_probe_start[], preemption_probe_end[];
extern const char __kernel_start[], __kernel_end[];
#define USER_DATA (RUM_USER_BASE + RUM_PAGE_SIZE)
#define USER_ESP (RUM_USER_STACK_TOP - 32u)
static task_id children[2];
static volatile uint32_t *data[2];
static uint32_t operation, rounds, nested_irqs, deferred_returns;
static struct task_event finished, unrelated;
static bool monitor_done;
static task_id sleeper;
static unsigned sleeper_returns;

static void check(bool condition, const char *name)
{
    if (condition) return;
    serial_writestring("rum_preemption_failed: ");
    serial_writestring(name);
    serial_writestring("\n");
    cpu_halt();
}

static void nested_irq(void)
{
    check(irq_in_handler(), "nested handler is marked active");
    struct task_snapshot before, after;
    check(task_snapshot_read(&before), "snapshot before nested IRQs");
    uint32_t pages = pmm_stats().free_pages;
    /* Exercise a timer entry while another IRQ handler is still on the stack.
       Kernel-CS returns must leave its expired scheduling request pending. */
    for (unsigned i = 0; i <= TASK_TIME_SLICE_TICKS; ++i)
        __asm__ volatile ("int $0x20" : : : "memory", "cc");
    check(task_snapshot_read(&after) && before.switches == after.switches &&
          before.current == after.current && pmm_stats().free_pages == pages &&
          !task_yield(), "nested timer IRQ cannot switch or allocate");
    ++nested_irqs;
}

void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) {
        irq_dispatch(frame);
        return;
    }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax != 0xfffff123u) { syscall_dispatch(frame); return; }

    /* A slice expires while this process is executing synchronous kernel work.
       Neither that IRQ nor releasing the hold can switch the continuation. */
    cpu_interrupt_enable();
    task_id owner = task_current_id();
    struct task_snapshot before, after;
    check(task_snapshot_read(&before) && task_switch_hold(), "syscall protected scope");
    void *temporary = kmalloc(64);
    check(temporary, "syscall temporary allocation");
    uint32_t started = timer_ticks();
    while ((uint32_t)(timer_ticks() - started) < TASK_TIME_SLICE_TICKS + 2u)
        __asm__ volatile ("hlt" : : : "memory");
    __asm__ volatile ("int $0x23" : : : "memory", "cc");
    check(task_snapshot_read(&after) && before.switches == after.switches &&
          task_current_id() == owner && !task_yield(), "kernel ticks only defer scheduling");
    check(kfree(temporary) && task_switch_release(), "release syscall temporary ownership");
    ++deferred_returns;
    frame->eax = 0;
    /* Production assembly masks IRQs and services the pending request only
       after this C call returns and all temporary ownership has ended. */
}

static task_id make_process(unsigned index, bool foreground)
{
    struct paging_space *space = paging_space_create();
    check(space && paging_user_allocate(space, RUM_USER_BASE, 2, PAGING_WRITABLE) &&
          paging_user_allocate(space, RUM_USER_STACK_TOP - RUM_PAGE_SIZE, 1, PAGING_WRITABLE),
          "private process pages");
    uint32_t physical;
    check(paging_translate(space, USER_DATA, &physical), "translate private counter page");
    data[index] = (volatile uint32_t *)(uintptr_t)physical;
    uint32_t seed = 0x11220000u + index * 0x100u;
    for (unsigned i = 0; i < 6; ++i) data[index][4 + i] = seed + i;
    data[index][10] = USER_ESP;
    data[index][100] = seed ^ 0x55aa55aau;
    check(paging_copy_to_user(space, RUM_USER_BASE, preemption_probe_start,
                             (size_t)(preemption_probe_end - preemption_probe_start)) &&
          paging_user_protect(space, RUM_USER_BASE, 1, 0), "read-only private code");
    struct task_process process = { .space = space };
    cpu_user_frame_initialize(&process.user_frame, RUM_USER_BASE, USER_ESP);
    process.user_frame.core.eax = seed;
    process.user_frame.core.ebx = seed + 1;
    process.user_frame.core.edx = seed + 2;
    process.user_frame.core.esi = seed + 3;
    process.user_frame.core.edi = seed + 4;
    process.user_frame.core.ebp = seed + 5;
    task_id id = foreground ? task_create_foreground_process(&process) : task_create_process(&process);
    check(id, "publish isolated process");
    struct task_information item;
    check(task_query(id, &item) && !paging_translate(space, RUM_USER_STACK_GUARD_BASE, NULL) &&
          !paging_translate(space, RUM_KERNEL_STACK_GUARD(item.stack_slot), NULL) &&
          !paging_user_accessible(space, item.stack_base, 1, false) &&
          !paging_user_accessible(space, RUM_KERNEL_LOAD_BASE, 1, false), "stack guards and supervisor mappings");
    return id;
}

static void inspect(unsigned index, uint32_t previous)
{
    struct task_information item;
    check(task_query(children[index], &item) && item.state == TASK_RUNNABLE &&
          data[index][0] > previous && !data[index][2], "both CPU-bound processes make progress");
    const struct exception_user_frame *frame = &item.user_frame;
    check(frame->core.eax == data[index][4] && frame->core.ebx == data[index][5] &&
          frame->core.edx == data[index][6] && frame->core.esi == data[index][7] &&
          frame->core.edi == data[index][8] && frame->core.ebp == data[index][9] &&
          frame->core.ecx <= 100000u, "all saved general registers remain process-private");
    check(frame->core.cs == USER_CODE_SELECTOR && frame->ss == USER_DATA_SELECTOR &&
          frame->core.ds == USER_DATA_SELECTOR && frame->core.es == USER_DATA_SELECTOR &&
          frame->core.fs == USER_DATA_SELECTOR && frame->core.gs == USER_DATA_SELECTOR &&
          (frame->core.eflags & 0x27602u) == 0x602u &&
          frame->esp >= USER_ESP - 4 && frame->esp <= USER_ESP &&
          frame->core.eip >= RUM_USER_BASE && frame->core.eip < RUM_USER_BASE + RUM_PAGE_SIZE,
          "complete saved user frame, DF/IF, privilege and stack");
    check(item.directory != paging_directory_address(paging_active_space()) &&
          rum_tss.esp0 != item.stack_top, "preempted CR3 and TSS are inactive");
}

static void unrelated_waiter(void *argument)
{
    (void)argument;
    uint32_t observed = task_event_sequence(&unrelated);
    check(task_wait(&unrelated, observed), "attach unrelated event waiter");
    ++sleeper_returns;
}

static void monitor(void *argument)
{
    (void)argument;
    uint32_t previous[2] = {0};
    uint32_t sequence = task_event_sequence(&unrelated);
    for (unsigned i = 0; i < 6; ++i) {
        inspect(0, previous[0]); inspect(1, previous[1]);
        previous[0] = data[0][0]; previous[1] = data[1][0];
        struct task_information boot, waiting;
        check(task_query(1, &boot) && boot.state == TASK_BLOCKED &&
              task_query(sleeper, &waiting) && waiting.state == TASK_BLOCKED && !sleeper_returns &&
              task_event_sequence(&unrelated) == sequence, "PIT does not wake unrelated child/event waiters");
        ++rounds;
        uint32_t started = timer_ticks();
        check(task_yield(), "round-robin kernel checkpoint");
        check((uint32_t)(timer_ticks() - started) >= 2u * TASK_TIME_SLICE_TICKS,
              "each non-yielding process gets a complete time slice");
    }
    if (!operation) check(task_cancel_foreground(), "cancel a suspended CPU-bound process");
    else data[0][1] = operation;
    struct task_information item;
    do {
        check(task_yield() && task_query(children[0], &item), "wait for selected process to terminate");
        check(data[1][0] > previous[1] && !data[1][2], "survivor progresses during sibling termination");
        previous[1] = data[1][0];
    } while (item.state != TASK_EXITED);
    check(!data[0][2] && data[0][100] == (data[0][4] ^ 0x55aa55aau) &&
          data[1][100] == (data[1][4] ^ 0x55aa55aau), "private canaries and user assertions");
    data[1][1] = 1;
    do { check(task_yield() && task_query(children[1], &item), "wait for survivor exit"); }
    while (item.state != TASK_EXITED);
    task_event_signal(&unrelated);
    while (task_query(sleeper, &item)) check(task_yield(), "reap unrelated event waiter");
    check(sleeper_returns == 1, "event waiter wakes only for its actual event");
    monitor_done = true;
    task_event_signal(&finished);
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize() &&
          task_initialize(), "memory and scheduler startup");
    serial_writestring("rum_preemption_ready\n");
    timer_initialize(); irq_register(3, nested_irq); pic_unmask(0); cpu_interrupt_enable();
    /* Warm the allocator once: heap pages stay mapped for reuse by design. */
    void *warmup = kmalloc(64);
    check(warmup && kfree(warmup), "warm heap");
    uint32_t baseline = pmm_stats().free_pages;
    struct heap_statistics heap = heap_stats();
    struct fs_statistics filesystem = fs_stats();
    const uint32_t cases[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        serial_writestring("rum_preemption_case ");
        char number[2] = { (char)('A' + c), 0 };
        serial_writestring(number); serial_writestring("\n");
        operation = cases[c]; monitor_done = false;
        sleeper_returns = 0;
        sleeper = task_create(unrelated_waiter, NULL, NULL);
        check(sleeper, "create independent event waiter");
        children[0] = make_process(0, true); children[1] = make_process(1, false);
        check(data[0] != data[1] && task_create(monitor, NULL, NULL), "independent physical pages and monitor");
        struct task_information first, second;
        check(task_wait_process(children[0], &first) && task_wait_process(children[1], &second),
              "parent waits independently of CPU-bound children");
        while (!monitor_done) {
            uint32_t observed = task_event_sequence(&finished);
            if (!monitor_done) check(task_wait(&finished, observed), "monitor completion");
        }
        if (!operation) check(first.termination == TASK_TERMINATION_CANCELLED, "cancellation result");
        else if (operation == 11) check(first.termination == TASK_TERMINATION_EXIT && first.exit_status == 37 &&
                                       deferred_returns == 1 && nested_irqs == 1, "deferred syscall return and nested IRQs");
        else {
            uint32_t expected = operation == 2 ? 6 : operation <= 4 ? 13 :
                                operation <= 6 ? 7 : operation == 7 ? 6 : 14;
            check(first.termination == TASK_TERMINATION_FAULT &&
                  (first.fault.vector == expected || (operation == 6 && first.fault.vector == 6)),
                  "CPU restrictions and mapping faults survive preemption");
        }
        check(second.termination == TASK_TERMINATION_EXIT && second.exit_status == 37,
              "unrelated process exits normally");
        check(task_foreground_end(children[0]) && task_reap_process(children[0]) && task_reap_process(children[1]),
              "reap inactive preempted owners");
        (void)task_reap();
        struct task_snapshot snapshot;
        check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.processes &&
              snapshot.created == snapshot.reaped && snapshot.created == snapshot.exited &&
              pmm_stats().free_pages == baseline && heap_stats().allocations == heap.allocations &&
              heap_stats().used_bytes == heap.used_bytes && fs_stats().references == filesystem.references &&
              fs_stats().objects == filesystem.objects, "all preempted process resources return to baseline");
    }
    check(rounds == 66, "repeat fair scheduling across faults, cancellation and normal exit");
    serial_writestring("rum_preemption_ok\n");
    cpu_halt();
}
