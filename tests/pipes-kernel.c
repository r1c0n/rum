#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/pipe.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "test-files.h"

#define PAGE 0x80800000u
#define TEST_QUERY 0xffffffe0u
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char pipes_asset_start[], pipes_asset_end[];
void kernel_main(uint32_t, uint32_t);
void pipes_syscall_dispatch(struct exception_frame *);
#define syscall_dispatch pipes_syscall_dispatch
#include "../kernel/process/syscall.c"
#undef syscall_dispatch
void syscall_dispatch(struct exception_frame *frame)
{
    if (frame && frame->eax == TEST_QUERY && frame->vector == RUM_SYSCALL_VECTOR && !frame->error &&
        exception_frame_from_user(frame) && task_current_is_process() && !irq_in_handler()) {
        struct pipe_statistics s = pipe_stats();
        frame->eax = frame->ebx == 0 ? s.pipes : frame->ebx == 1 ? s.read_waiters :
            frame->ebx == 2 ? s.write_waiters : frame->ebx == 3 ? s.operations :
            frame->ebx == 4 ? s.buffered_bytes : process_handles_stats().descriptors;
        return;
    }
    pipes_syscall_dispatch(frame);
}
static void check(bool ok, const char *message)
{
    if (!ok) { serial_writestring("rum_pipes_failed: "); serial_writestring(message); serial_writestring("\n"); cpu_halt(); }
}
static void number(uint32_t value)
{
    char text[12]; unsigned n = 0;
    do { text[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) serial_putchar(text[--n]);
}
static void exercise(const char *mode)
{
    struct heap_statistics heap = heap_stats();
    struct fs_statistics fs = fs_stats();
    uint32_t pages = pmm_stats().free_pages;
    struct rum_arguments arguments = { .argc = 2, .string_bytes = 10, .offsets = {0, 10}, .strings = "/PIPE.ELF" };
    for (unsigned i = 0;; ++i) { arguments.strings[arguments.string_bytes++] = mode[i]; if (!mode[i]) break; }
    struct task_process process;
    check(elf_load_process(pipes_asset_start, pipes_asset_end - pipes_asset_start, &arguments, &process) &&
          paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
          paging_user_protect(process.space, PAGE + 8192, 1, 0), "load parent and cross-page buffers");
    task_id parent = task_create_foreground_process(&process);
    check(parent != 0, "publish parent");
    bool cancel = mode[0] == 'c';
    if (cancel) {
        bool waiting = false;
        for (unsigned i = 0; i < RUM_TASK_CAPACITY * 4u && !waiting; ++i) {
            check(task_yield(), "run blocking parent");
            struct pipe_statistics s = pipe_stats();
            waiting = s.operations == 1 && s.read_waiters + s.write_waiters == 1;
        }
        check(waiting && task_cancel_foreground(), "cancel active pipe syscall");
    }
    struct task_information info;
    check(task_wait_process(parent, &info), "wait for parent");
    if (info.exit_status) { serial_writestring("rum_pipes_user_status: "); number((uint32_t)info.exit_status); serial_writestring("\n"); }
    check(info.termination == (cancel ? TASK_TERMINATION_CANCELLED : TASK_TERMINATION_EXIT) && !info.exit_status &&
          task_handle_count(parent) == 0, "user assertions and endpoint teardown");
    check(task_foreground_end(parent) && task_reap_process(parent), "collect parent");
    struct task_snapshot snapshot;
    for (unsigned i = 0; i < RUM_TASK_CAPACITY * 3u; ++i) {
        check(task_snapshot_read(&snapshot), "orphan snapshot");
        if (!snapshot.processes) break;
        check(task_yield(), "drain orphaned waiters");
    }
    struct pipe_statistics pipes = pipe_stats();
    struct process_handle_statistics handles = process_handles_stats();
    check(!snapshot.processes && !snapshot.event_waiters && !snapshot.deadline_waiters &&
          !pipes.pipes && !pipes.operations && !pipes.read_waiters && !pipes.write_waiters && !pipes.buffered_bytes &&
          !handles.descriptions && !handles.descriptors && !handles.aliases &&
          fs_stats().references == fs.references && fs_stats().objects == fs.objects &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "pipe event handle object heap and page ledgers return to baseline");
    serial_writestring("rum_pipes_case_ok: "); serial_writestring(mode); serial_writestring("\n");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && test_files_mount() && task_initialize(), "startup");
    check(test_file_put("PIPE.ELF", pipes_asset_start, pipes_asset_end - pipes_asset_start), "test-owned executable");
    cpu_interrupt_enable(); pic_unmask(0);
    for (unsigned i = 0; i < 4; ++i) {
        exercise("parent"); exercise("cancelread"); exercise("cancelwrite"); exercise("orphan");
    }
    serial_writestring("rum_pipes_ok\n"); cpu_halt();
}
