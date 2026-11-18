/* Runs in the scheduler wait fixture, through real kernel stacks and IRQ entry. */
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/handles.h>
#include <rum/serial.h>
#include <rum/sync.h>
#include <rum/task.h>
#include <rum/timer.h>

void sync_checks(void);
static struct object_handles table;
static struct kernel_object *irq_pin;
static struct task_snapshot snapshot;
static struct rum_sync_request request(unsigned operation, unsigned type, rum_object_handle_t handle)
{
    return (struct rum_sync_request){ .version = RUM_THREAD_ABI_VERSION, .size = sizeof(struct rum_sync_request),
        .operation = operation, .type = type, .handle = handle };
}
static void check(bool ok, const char *message)
{
    if (ok) return;
    serial_writestring("rum_waits_failed: "); serial_writestring(message); serial_writestring("\n");
    cpu_halt();
}
static rum_object_handle_t create(unsigned type, unsigned flags, unsigned value, unsigned maximum)
{
    struct rum_sync_request r = request(RUM_SYNC_CREATE, type, 0);
    r.flags = flags; r.value = value; r.maximum = maximum;
    check(!sync_request(&table, &r), "create synchronization object");
    return r.result_handle;
}
static rum_result_t operate(unsigned operation, unsigned type, rum_object_handle_t handle, uint32_t amount)
{
    struct rum_sync_request r = request(operation, type, handle);
    if (operation == RUM_SYNC_WAIT) r.timeout_ms = amount;
    else r.value = amount;
    return sync_request(&table, &r);
}
static void close_handle(rum_object_handle_t h)
{
    struct rum_object_request r = { .version = 1, .size = sizeof r, .operation = RUM_OBJECT_CLOSE, .handle = h };
    check(!object_handle_request(&table, &r), "close sync handle");
}
static void clean(void)
{
    (void)task_reap();
    struct object_statistics stats = object_stats();
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.event_waiters &&
          !snapshot.deadline_waiters && !stats.total.objects && !object_handles_count(&table),
          "sync wait, thread, object and table ledger returns to baseline");
    for (unsigned i = 0; i < OBJECT_REFERENCE_COUNT; ++i)
        check(!stats.total.references[i], "no retained sync reference");
}
struct waiter {
    rum_object_handle_t handle;
    unsigned type;
    uint32_t timeout;
    rum_result_t result;
    bool done;
};
static void worker(void *argument)
{
    struct waiter *w = argument;
    w->result = operate(RUM_SYNC_WAIT, w->type, w->handle, w->timeout);
    w->done = true;
}
static void block_worker(struct waiter *w, unsigned type, rum_object_handle_t h, uint32_t timeout)
{
    *w = (struct waiter){ .handle = h, .type = type, .timeout = timeout };
    task_id id = task_create(worker, w, NULL);
    struct task_information info;
    check(id && task_yield() && task_query(id, &info) && info.state == TASK_BLOCKED && !w->done,
          "synchronization waiter published exactly once");
}
static void irq_signal(void)
{
    uint32_t pages = pmm_stats().free_pages;
    task_id id = task_current_id();
    check(irq_in_handler() && operate(RUM_SYNC_WAIT, OBJECT_EVENT, 0, 0) == -RUM_EBUSY &&
          sync_signal_pinned(irq_pin, 1) == 0 &&
          id == task_current_id() && pmm_stats().free_pages == pages, "IRQ handoff without allocation or switch");
}
static void events(void)
{
    for (unsigned manual = 0; manual <= 1; ++manual) {
        rum_object_handle_t h = create(OBJECT_EVENT, manual, 0, 0);
        struct waiter a, b;
        check(operate(RUM_SYNC_WAIT, OBJECT_EVENT, h, 0) == -RUM_ETIMEDOUT, "empty event poll");
        check(!operate(RUM_SYNC_SIGNAL, OBJECT_EVENT, h, 0) && !operate(RUM_SYNC_SIGNAL, OBJECT_EVENT, h, 0) &&
              !operate(RUM_SYNC_WAIT, OBJECT_EVENT, h, 0) &&
              operate(RUM_SYNC_WAIT, OBJECT_EVENT, h, 0) == (manual ? 0 : -RUM_ETIMEDOUT),
              "signals before registration coalesce");
        check(!operate(RUM_SYNC_RESET, OBJECT_EVENT, h, 0), "reset signaled event");
        block_worker(&a, OBJECT_EVENT, h, RUM_WAIT_FOREVER);
        block_worker(&b, OBJECT_EVENT, h, RUM_WAIT_FOREVER);
        check(!object_handle_acquire(&table, h, OBJECT_EVENT, RUM_RIGHT_SIGNAL, &irq_pin), "pin IRQ event");
        __asm__ volatile ("int $0x23" : : : "memory", "cc");
        check(operate(RUM_SYNC_WAIT, OBJECT_EVENT, h, 0) == (manual ? 0 : -RUM_ETIMEDOUT),
              "new arrival cannot steal FIFO grant");
        if (manual) check(!operate(RUM_SYNC_RESET, OBJECT_EVENT, h, 0), "reset after broadcast");
        check(task_yield() && a.done && !a.result && b.done == (bool)manual, "FIFO or manual broadcast");
        close_handle(h);
        if (!manual) {
            check(irq_pin->owners[OBJECT_OPERATION] == 2, "final close preserves blocked operation and IRQ pin");
            __asm__ volatile ("int $0x23" : : : "memory", "cc");
        }
        check(!object_release(irq_pin, OBJECT_OPERATION), "drop IRQ pin in task context"); irq_pin = NULL;
        check(task_yield() && b.done && !b.result, "last waiter survives reset or close");
        clean();
    }
}
static void semaphores(void)
{
    rum_object_handle_t h = create(OBJECT_SEMAPHORE, 0, 1, UINT32_MAX);
    check(operate(RUM_SYNC_SIGNAL, OBJECT_SEMAPHORE, h, UINT32_MAX) == -RUM_EOVERFLOW &&
          !operate(RUM_SYNC_WAIT, OBJECT_SEMAPHORE, h, 0), "overflow preserves prior permit");
    struct waiter a, b;
    block_worker(&a, OBJECT_SEMAPHORE, h, 100);
    block_worker(&b, OBJECT_SEMAPHORE, h, RUM_WAIT_FOREVER);
    uint32_t flags = cpu_interrupt_save();
    task_timer_tick(timer_ticks() + TIMER_HZ);
    cpu_interrupt_restore(flags);
    check(!operate(RUM_SYNC_SIGNAL, OBJECT_SEMAPHORE, h, 2), "skip timed-out waiter during release");
    check(!operate(RUM_SYNC_WAIT, OBJECT_SEMAPHORE, h, 0) &&
          operate(RUM_SYNC_WAIT, OBJECT_SEMAPHORE, h, 0) == -RUM_ETIMEDOUT, "one grant and one spare permit");
    check(task_yield() && a.done && a.result == -RUM_ETIMEDOUT && b.done && !b.result, "timeout wins once");
    check(!operate(RUM_SYNC_SIGNAL, OBJECT_SEMAPHORE, h, UINT32_MAX), "maximum release is bounded");
    close_handle(h); clean();
}
static void mutexes(void)
{
    rum_object_handle_t h = create(OBJECT_MUTEX, 0, 0, 0);
    check(operate(RUM_SYNC_SIGNAL, OBJECT_MUTEX, h, 0) == -RUM_EACCES &&
          !operate(RUM_SYNC_WAIT, OBJECT_MUTEX, h, 0) &&
          operate(RUM_SYNC_WAIT, OBJECT_MUTEX, h, 0) == -RUM_EBUSY, "mutex ownership and recursion");
    struct waiter a, b;
    block_worker(&a, OBJECT_MUTEX, h, RUM_WAIT_FOREVER);
    block_worker(&b, OBJECT_MUTEX, h, RUM_WAIT_FOREVER);
    check(!operate(RUM_SYNC_SIGNAL, OBJECT_MUTEX, h, 0) &&
          operate(RUM_SYNC_SIGNAL, OBJECT_MUTEX, h, 0) == -RUM_EACCES, "handoff changes owner immediately");
    check(task_yield() && a.done && !a.result && b.done && b.result == RUM_WAIT_ABANDONED,
          "owner exit hands abandoned mutex to next waiter");
    check(operate(RUM_SYNC_WAIT, OBJECT_MUTEX, h, 0) == RUM_WAIT_ABANDONED &&
          !operate(RUM_SYNC_SIGNAL, OBJECT_MUTEX, h, 0) && !operate(RUM_SYNC_WAIT, OBJECT_MUTEX, h, 0),
          "unobserved abandonment persists for exactly one acquisition");
    close_handle(h);
    check(object_stats().total.references[OBJECT_OWNER] == 1, "last close preserves owner pin");
    sync_abandon_current(); clean();
}
static void rights_and_bounds(void)
{
    rum_object_handle_t h = create(OBJECT_MUTEX, 0, 0, 0);
    struct rum_object_request dup = { .version = 1, .size = sizeof dup, .operation = RUM_OBJECT_DUPLICATE,
        .handle = h, .rights = RUM_RIGHT_WAIT, .flags = RUM_OBJECT_INHERIT };
    check(object_handle_request(&table, &dup) == -RUM_EACCES, "reject inheritable sync alias");
    dup.flags = 0;
    check(!object_handle_request(&table, &dup) &&
          operate(RUM_SYNC_WAIT, OBJECT_MUTEX, dup.result_handle, 0) == -RUM_EACCES &&
          operate(RUM_SYNC_QUERY, OBJECT_MUTEX, dup.result_handle, 0) == -RUM_EACCES,
          "wait-only mutex cannot acquire or query");
    static struct process_handles descriptors;
    struct rum_resource_information info;
    check(!process_resources_read(&descriptors, &table, &info) && info.objects == 1 &&
          info.typed_handles == 2 && info.table_references == 2, "released diagnostics count distinct sync objects");
    for (unsigned i = 0; i < RUM_RESOURCE_TYPE_COUNT; ++i) check(!info.types[i], "legacy bins unchanged");
    check(operate(RUM_SYNC_WAIT, OBJECT_MUTEX, h, RUM_SLEEP_MAX_MS + 1) == -RUM_ERANGE &&
          operate(RUM_SYNC_RESET, OBJECT_MUTEX, h, 0) == -RUM_EINVAL, "invalid timeout or operation");
    close_handle(h); close_handle(dup.result_handle); clean();

    static struct object_handles tables[SYNC_OBJECT_LIMIT / RUM_OBJECT_HANDLE_LIMIT + 1];
    for (unsigned i = 0; i <= SYNC_OBJECT_LIMIT; ++i) {
        struct rum_sync_request r = request(RUM_SYNC_CREATE, OBJECT_EVENT, 0);
        check(sync_request(&tables[i / RUM_OBJECT_HANDLE_LIMIT], &r) ==
              (i == SYNC_OBJECT_LIMIT ? -RUM_ENOMEM : 0), "bounded synchronization pool");
        if (i + 1 == RUM_OBJECT_HANDLE_LIMIT) {
            r.result_handle = 0;
            check(sync_request(&tables[0], &r) == -RUM_EMFILE && !r.result_handle &&
                  object_stats().kinds[OBJECT_EVENT].objects == RUM_OBJECT_HANDLE_LIMIT,
                  "full table rolls back private constructor");
        }
    }
    for (unsigned i = 0; i < sizeof tables / sizeof tables[0]; ++i)
        check(!object_handles_destroy(&tables[i]), "release pool");
    clean();
}
void sync_checks(void)
{
    irq_register(3, irq_signal);
    rights_and_bounds();
    events();
    semaphores();
    mutexes();
    serial_writestring("rum_sync_ok\n");
}
