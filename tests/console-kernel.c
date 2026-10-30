/* Inspect the construction switch hold only in this fixture. Production has
   no test syscalls, console ownership setters or backend injection hooks. */
#include <rum/fs.h>
static enum fs_error fixture_clone(const struct fs_context *, struct fs_context *);
#define fs_context_clone fixture_clone
#include "../kernel/process/task.c"
#undef fs_context_clone
#include <rum/abi/syscall.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/terminal.h>
#include "fs-backend.h"

#define TEST_ALONE 0xffffffe0u
#define TEST_CONSTRUCT 0xffffffe1u
#define TEST_IO 0xffffffe2u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char console_asset_start[], console_asset_end[];
static struct fs_operations operations;
static bool arm_construct, arm_io;
static unsigned constructions, reads;
static struct task_snapshot snapshot;
static struct rum_arguments arguments;
static void check(bool ok, const char *name)
{
    if (ok) return;
    serial_writestring("rum_console_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static void cancel_during_io(const char *marker)
{
    uint32_t started = timer_ticks(), before = switches; task_id id = task_current_id();
    check((fs_operation_active() || current->switch_holds) && !irq_in_handler(), "protected synchronous task context");
    serial_writestring(marker); serial_writestring("\n");
    while (!task_current_cancelled() || (uint32_t)(timer_ticks() - started) < 6) {
        check((uint32_t)(timer_ticks() - started) < TIMER_HZ * 10u, "real keyboard cancellation arrived");
        __asm__ volatile("pause");
    }
    check(id == task_current_id() && switches == before && !task_yield(),
          "pending cancellation does not preempt or yield inside a backend operation");
}
static enum fs_error fixture_clone(const struct fs_context *source, struct fs_context *destination)
{
    enum fs_error error = fs_context_clone(source, destination);
    if (!error && arm_construct && current->switch_holds) {
        arm_construct = false; ++constructions; cancel_during_io("rum_console_constructor_ready");
    }
    return error;
}
static struct fs_io_result read_node(void *context, uint64_t id, uint64_t offset, void *buffer, size_t bytes)
{
    if (arm_io) { arm_io = false; ++reads; cancel_during_io("rum_console_io_ready"); }
    return fs_test_backend()->operations->read(context, id, offset, buffer, bytes);
}
void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) { irq_dispatch(frame); return; }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax == TEST_ALONE) {
        struct console_statistics s = console_stats();
        frame->eax = s.sessions == 1 && !s.closing && s.members == 1 && s.session_references == 2 &&
            s.process_references == 2 && s.owner == current->id && s.shell == current->id;
    } else if (frame->eax == TEST_CONSTRUCT) { arm_construct = true; frame->eax = 0; }
    else if (frame->eax == TEST_IO) { arm_io = true; frame->eax = 0; }
    else syscall_dispatch(frame);
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(keyboard_initialize() && magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && ramfs_initialize() && task_initialize(), "fixture initialization");
    check(ramfs_put("console.elf", console_asset_start, console_asset_end - console_asset_start) &&
          ramfs_put("data.bin", "abc", 3), "RAM fixtures");
    fs_test_backend_initialize(); operations = *fs_test_backend()->operations;
    operations.read = read_node;
    struct fs_backend backend = *fs_test_backend(); backend.operations = &operations;
    cpu_interrupt_enable(); check(fs_mount_disk(&backend) == FS_OK, "disk fixture mount");
    pic_unmask(0); pic_unmask(1);
    uint32_t pages = pmm_stats().free_pages; struct heap_statistics heap = heap_stats(); struct fs_statistics fs = fs_stats();
    arguments = (struct rum_arguments){ .argc = 3, .string_bytes = 19, .offsets = {0, 13, 18},
        .strings = "/console.elf\0main\0" };
    struct task_process process;
    check(elf_load_process(console_asset_start, console_asset_end - console_asset_start, &arguments, &process), "load ring-3 shell");
    task_id shell = task_create_shell_process(&process); check(shell, "publish protected initial shell atomically");
    struct task_information info; check(task_wait_process(shell, &info), "shell completion");
    if (info.exit_status) {
        char digits[11]; unsigned n = 0, value = (unsigned)info.exit_status;
        do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
        serial_writestring("user status: "); while (n) serial_putchar(digits[--n]); serial_writestring("\n");
    }
    check(info.termination == TASK_TERMINATION_EXIT && !info.exit_status && task_foreground_end(shell) &&
          task_reap_process(shell), "user assertions and shell reap");
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.processes && !snapshot.event_waiters &&
          !snapshot.deadline_waiters && snapshot.created == snapshot.exited && snapshot.exited == snapshot.reaped &&
          !snapshot.console.sessions && !snapshot.console.members && !snapshot.console.session_references &&
          !snapshot.console.process_references && !arm_construct && !arm_io && constructions == 1 && reads == 1 &&
          !fs_test_backend_pins() && fs_stats().objects == fs.objects && fs_stats().references == fs.references &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "sessions, handles, backend pins, waits, stacks and private pages return to baseline");
    check(fs_unmount_disk() == FS_OK, "disk fixture unmount");
    serial_writestring("rum_console_ok\n"); cpu_halt();
}
