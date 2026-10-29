/* Seed the private PIT clock only inside this fixture. Production exports no
   clock setter or wait-registration pointer. Other tests link normal objects. */
#include "../kernel/process/task.c"
#include "../hal/pc/pit.c"
#include <rum/heap.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>

void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
static struct task_event event, done, unrelated;
static volatile unsigned completed, idle_ticks, tie;
static volatile task_id tie_target;
static uint32_t free_baseline;
struct plan { struct task_event *event; uint32_t duration; bool timed; enum task_wait_result expected; };

static void check(bool condition, const char *name)
{
    if (condition) return;
    serial_writestring("rum_waits_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static void fixture_tick(void)
{
    task_id owner = task_current_id();
    uint32_t before = switches, pages = pmm_stats().free_pages;
    bool due = false;
    for (unsigned i = 0; i < TASK_SLOTS; ++i)
        if (tasks[i].id == tie_target && tasks[i].state == TASK_BLOCKED && tasks[i].deadline_active &&
            tasks[i].deadline == ticks + 1) due = true;
    if (due && tie == 1) task_event_signal(&event);
    timer_interrupt(); /* Actual counter update, deadline scan and UI event. */
    if (due && tie == 2) task_event_signal(&event);
    if (due) tie = 0;
    if (owner == 2) ++idle_ticks;
    check(irq_in_handler() && task_current_id() == owner && switches == before &&
          pmm_stats().free_pages == pages && task_wait_for(NULL, 0, 1) == TASK_WAIT_INVALID,
          "PIT never switches, allocates or blocks in handler context");
}
static void ledger(void)
{
    struct task_snapshot snapshot;
    (void)task_reap();
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.event_waiters &&
          !snapshot.deadline_waiters && snapshot.created == snapshot.exited && snapshot.created == snapshot.reaped &&
          pmm_stats().free_pages == free_baseline, "all stack pages and wait registrations return to baseline");
}
static void worker(void *argument)
{
    const struct plan *plan = argument;
    uint32_t sequence = task_event_sequence(plan->event);
    enum task_wait_result reason = plan->timed ? task_wait_for(plan->event, sequence, plan->duration) :
        task_wait(plan->event, sequence) ? TASK_WAIT_EVENT : TASK_WAIT_INVALID;
    check(reason == plan->expected && !current->waiting && !current->deadline_active && !current->deadline,
          "wake reason and registration cleared before worker resumes");
    uint32_t flags = cpu_interrupt_save(); cpu_interrupt_restore(flags);
    check((flags & IF) && rum_tss.esp0 == current->stack_top &&
          paging_active_space() == current->process->space, "wait restores IF, private stack, CR3 and TSS");
    ++completed; task_event_signal(&done);
}
static void blocked(task_id id)
{
    struct task_information info;
    do { check(task_yield() && task_query(id, &info), "worker reaches its event wait"); }
    while (info.state != TASK_BLOCKED);
}
static void finish(unsigned count)
{
    while (completed != count) {
        uint32_t sequence = task_event_sequence(&done);
        if (completed != count) check(task_wait(&done, sequence), "await worker completion");
    }
}
static void software_tick(void *unused)
{
    (void)unused;
    __asm__ volatile ("int $0x20" : : : "memory", "cc");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(task_wait_for(NULL, 0, 1) == TASK_WAIT_INVALID && task_sleep(1) == -RUM_EBUSY,
          "uninitialized waits refuse registration");
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start,
                         (uintptr_t)__kernel_end) && paging_initialize() && heap_initialize() && task_initialize(),
          "wait fixture startup");
    irq_register(0, fixture_tick); cpu_interrupt_enable();
    free_baseline = pmm_stats().free_pages;
    uint32_t before = switches;
    check(task_wait_for(NULL, 0, 0) == TASK_WAIT_TIMEOUT &&
          task_wait_for(NULL, 0, TIMER_HALF_RANGE) == TASK_WAIT_INVALID && switches == before,
          "zero timeout and half-range rejection do not switch");
    event.sequence = UINT32_MAX; task_event_signal(&event);
    check(task_wait_for(&event, UINT32_MAX, 0) == TASK_WAIT_EVENT && !event.sequence && switches == before,
          "event before registration and sequence wrap beat an immediate timeout");
    /* Hardware IRQ0 stays masked for this exact zero-deadline boundary. A
       runnable worker delivers one software PIT IRQ after BOOT has blocked. */
    ticks = UINT32_MAX;
    check(task_create(software_tick, NULL, NULL) && task_wait_for(NULL, 0, 1) == TASK_WAIT_TIMEOUT &&
          !ticks && !current->deadline_active && !current->deadline, "deadline zero is valid across clock wrap");
    ledger();
    ticks = UINT32_MAX - 4; pic_unmask(0);
    uint32_t started = timer_ticks();
    check(task_wait_for(NULL, 0, 8) == TASK_WAIT_TIMEOUT && (uint32_t)(timer_ticks() - started) >= 8 &&
          timer_ticks() < started && idle_ticks >= 8, "real PIT wrap, expired idle budget and STI/HLT wakeup");
    ledger();
    for (unsigned mode = 1; mode <= 2; ++mode) {
        struct plan plan = { &event, 3, true, mode == 1 ? TASK_WAIT_EVENT : TASK_WAIT_TIMEOUT };
        unsigned target = completed + 1;
        tie = mode; tie_target = task_create(worker, &plan, NULL);
        check(tie_target, "publish simultaneous event/deadline waiter");
        finish(target); check(!tie, "boundary IRQ ran"); ledger();
    }
    struct plan group[5] = {
        { &event, 100, true, TASK_WAIT_EVENT }, { &event, 100, true, TASK_WAIT_EVENT },
        { &event, 100, true, TASK_WAIT_EVENT }, { &event, 0, false, TASK_WAIT_EVENT },
        { &unrelated, 0, false, TASK_WAIT_EVENT },
    };
    task_id ids[5]; unsigned target = completed + 4;
    for (unsigned i = 0; i < 5; ++i) { ids[i] = task_create(worker, &group[i], NULL); check(ids[i], "broadcast worker"); }
    for (unsigned i = 0; i < 5; ++i) blocked(ids[i]);
    struct task_snapshot snapshot;
    check(task_snapshot_read(&snapshot) && snapshot.event_waiters == 5 && snapshot.deadline_waiters == 3,
          "independent event and deadline accounting");
    task_event_signal(&event); finish(target);
    check(task_sleep(40) == 0, "let PIT ticks pass the unrelated waiter");
    struct task_information info;
    check(task_query(ids[4], &info) && info.state == TASK_BLOCKED &&
          task_snapshot_read(&snapshot) && snapshot.event_waiters == 1 && !snapshot.deadline_waiters,
          "timer ticks and another event never wake an unrelated waiter");
    task_event_signal(&unrelated); finish(target + 1); ledger();
    /* Release a very distant deadline by signalling, reuse its slot for an
       untimed wait, then pass the old expiry. It must not wake the new owner. */
    pic_mask(0);
    struct plan distant = { &event, TIMER_HALF_RANGE - 1, true, TASK_WAIT_EVENT };
    task_id old = task_create(worker, &distant, NULL); check(old, "distant waiter"); blocked(old);
    uint32_t old_deadline = 0, old_slot = 0;
    for (unsigned i = 2; i < TASK_SLOTS; ++i) if (tasks[i].id == old) { old_deadline = tasks[i].deadline; old_slot = i; }
    task_event_signal(&event); finish(completed + 1); ledger();
    struct plan replacement = { &unrelated, 0, false, TASK_WAIT_EVENT };
    task_id newer = task_create(worker, &replacement, NULL); check(newer && newer != old, "fresh waiter identity"); blocked(newer);
    check(tasks[old_slot].id == newer && !tasks[old_slot].deadline_active, "slot reused without its old deadline");
    ticks = old_deadline;
    __asm__ volatile ("int $0x20" : : : "memory", "cc");
    check(task_query(newer, &info) && info.state == TASK_BLOCKED, "stale deadline cannot wake a reused slot");
    task_event_signal(&unrelated); finish(completed + 1); ledger(); pic_unmask(0);
    for (unsigned i = 0; i < 32; ++i) {
        check(!task_sleep(1) && !task_sleep(0), "repeat sleep and zero-duration yield"); ledger();
    }
    uint32_t saved = cpu_interrupt_save();
    check(task_wait_for(&event, event.sequence, 1) == TASK_WAIT_INVALID && task_sleep(1) == -RUM_EBUSY,
          "IF-clear callers cannot attach deadlines");
    cpu_interrupt_restore(saved);
    serial_writestring("rum_waits_ok\n"); cpu_halt();
}
