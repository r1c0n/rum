/* The clock callback is wrapped only to inject cancellation immediately after
   expiry. Syscall dispatch and scheduling use their normal production objects. */
#include "../hal/pc/pit.c"
#include <rum/cpu.h>
#include <rum/abi/syscall.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/keyboard.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/terminal.h>

#define PAGE 0x80800000u
#define TEST_CLOCK 0xfffffff0u
#define TEST_EVENT 0xfffffff1u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char sleep_asset_start[], sleep_asset_end[];
static struct task_event event;
static volatile bool cancel_expired;
static task_id target;
static volatile unsigned late_cancels, event_cancels;
static struct task_snapshot snapshot;
static struct rum_arguments arguments;

static void check(bool condition, const char *name)
{
    if (condition) return;
    serial_writestring("rum_sleep_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static void tick(void)
{
    timer_interrupt();
    if (!cancel_expired) return;
    struct task_information info;
    check(task_snapshot_read(&snapshot) && task_query(target, &info), "IRQ snapshot of timed process");
    if (info.state == TASK_RUNNABLE && !snapshot.deadline_waiters) {
        check(task_cancel_foreground(), "cancel after timeout made process runnable");
        cancel_expired = false; ++late_cancels;
    }
}
void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) { irq_dispatch(frame); return; }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax == TEST_CLOCK) { frame->eax = timer_ticks(); return; }
    if (frame->eax != TEST_EVENT) { syscall_dispatch(frame); return; }
    cpu_interrupt_enable();
    void *temporary = kmalloc(64); check(temporary, "temporary ownership during a cancellable wait");
    if (frame->ebx) check(task_cancel_foreground(), "cancellation before event registration");
    enum task_wait_result reason = task_wait_for(&event, task_event_sequence(&event), TIMER_HZ * 60u);
    check(reason == TASK_WAIT_EVENT || reason == TASK_WAIT_CANCELLED, "event wait returns to its kernel owner");
    check(task_snapshot_read(&snapshot) && !snapshot.deadline_waiters && kfree(temporary),
          "deadline removed and temporary resource released before cancellation exit");
    if (reason == TASK_WAIT_CANCELLED) ++event_cancels;
    frame->eax = reason;
}
static void blocked(task_id id)
{
    struct task_information info;
    do { check(task_yield() && task_query(id, &info) && info.state != TASK_EXITED, "process enters a blocking wait"); }
    while (info.state != TASK_BLOCKED);
}
static void exercise(const char *mode, unsigned action)
{
    serial_writestring("rum_sleep_case "); serial_writestring(mode); serial_writestring("\n");
    uint32_t pages = pmm_stats().free_pages;
    struct heap_statistics heap = heap_stats(); struct fs_statistics fs = fs_stats();
    arguments = (struct rum_arguments){ .argc = 2, .string_bytes = 5, .offsets = {0, 5}, .strings = "wait" };
    const char *text = mode;
    do { arguments.strings[arguments.string_bytes++] = *text; } while (*text++);
    struct task_process process;
    struct rum_sleep_request readonly = {1, sizeof readonly, 1, 0, {0, 0}};
    check(elf_load_process(sleep_asset_start, sleep_asset_end - sleep_asset_start, &arguments, &process) &&
          paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
          paging_copy_to_user(process.space, PAGE + 8192, &readonly, sizeof readonly) &&
          paging_user_protect(process.space, PAGE + 8192, 1, 0), "user image and validation pages");
    target = task_create_foreground_process(&process); check(target, "publish timed process");
    if (action) {
        blocked(target);
        check(task_snapshot_read(&snapshot) && snapshot.deadline_waiters == 1 && task_handle_count(target) == 4,
              "blocked process retains its stack, address space and open handles");
        uint32_t held = pmm_stats().free_pages;
        if (action == 1) {
            check(task_cancel_foreground() && pmm_stats().free_pages == held && task_handle_count(target) == 4 &&
                  task_snapshot_read(&snapshot) && !snapshot.deadline_waiters,
                  "cancel wakes and detaches without destroying active ownership");
        } else if (action == 2) task_event_signal(&event);
        else if (action == 3) cancel_expired = true;
        else serial_writestring("rum_sleep_keyboard_ready\n");
    }
    struct task_information info;
    check(task_wait_process(target, &info), "wait for timed process");
    if (info.exit_status) {
        char digits[11]; unsigned value = (unsigned)info.exit_status, n = 0;
        do { digits[n++] = (char)('0' + value % 10u); value /= 10u; } while (value);
        serial_writestring("user status: "); while (n) serial_putchar(digits[--n]); serial_writestring("\n");
    }
    bool cancelled = action == 1 || action == 3 || action == 4 || mode[0] == 'p';
    check(info.termination == (cancelled ? TASK_TERMINATION_CANCELLED : TASK_TERMINATION_EXIT) &&
          !info.exit_status && !cancel_expired && task_foreground_end(target) && task_reap_process(target),
          "normal completion or safe cancellation before user resumption");
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.event_waiters &&
          !snapshot.deadline_waiters && snapshot.created == snapshot.exited && snapshot.exited == snapshot.reaped &&
          fs_stats().objects == fs.objects && fs_stats().references == fs.references &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "waits, deadlines, handles, pins, heap and physical pages return to baseline");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(keyboard_initialize() && magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && ramfs_initialize() && task_initialize(), "sleep fixture boot");
    check(ramfs_put("wait.elf", sleep_asset_start, sleep_asset_end - sleep_asset_start) &&
          ramfs_put("data.bin", "abc", 3), "user fixtures");
    irq_register(0, tick); cpu_interrupt_enable(); pic_unmask(0); pic_unmask(1);
    exercise("normal", 0);
    for (unsigned i = 0; i < 8; ++i) {
        exercise("long", 1); exercise("eventcancel", 1); exercise("precancel", 0); exercise("event", 2);
    }
    exercise("latecancel", 3);
    check(late_cancels == 1 && event_cancels == 16, "cancellation before registration and after wakeup");
    exercise("long", 4);
    serial_writestring("rum_sleep_ok\n"); cpu_halt();
}
