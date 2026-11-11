#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "fs-backend.h"
#include "test-files.h"

#define PAGE 0x80800000u
#define TEST_CALLS 0xffffffe0u
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char shared_handles_asset_start[], shared_handles_asset_end[];
void kernel_main(uint32_t, uint32_t);
void shared_handles_syscall_dispatch(struct exception_frame *);
/* A test-only read counter proves invalid packets never reach a backend. */
#define syscall_dispatch shared_handles_syscall_dispatch
#include "../kernel/process/syscall.c"
#undef syscall_dispatch
void syscall_dispatch(struct exception_frame *frame)
{
    if (frame && frame->eax == TEST_CALLS && frame->vector == RUM_SYSCALL_VECTOR && !frame->error &&
        exception_frame_from_user(frame) && task_current_is_process() && !irq_in_handler()) {
        frame->eax = fs_test_backend_data_calls(); return;
    }
    shared_handles_syscall_dispatch(frame);
}
static void check(bool ok, const char *message)
{
    if (!ok) { serial_writestring("rum_shared_handles_failed: "); serial_writestring(message); serial_writestring("\n"); cpu_halt(); }
}
static void status(rum_result_t value)
{
    char digits[12]; unsigned count = 0;
    uint32_t number = (uint32_t)value;
    do { digits[count++] = (char)('0' + number % 10); number /= 10; } while (number);
    while (count) serial_putchar(digits[--count]);
}
static struct rum_arguments parent_args(void)
{
    return (struct rum_arguments){ .argc = 2, .string_bytes = 16, .offsets = {0, 9}, .strings = "/MAP.ELF\0parent" };
}
static bool load(struct task_process *process, const char *mode)
{
    struct rum_arguments arguments = parent_args();
    memcpy(arguments.strings + 9, mode, 7);
    return elf_load_process(shared_handles_asset_start, shared_handles_asset_end - shared_handles_asset_start, &arguments, process);
}
static uint32_t consume(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) { *(uint32_t *)(uintptr_t)page = head; head = page; }
    return head;
}
static void rollback(void)
{
    struct process_handles source = {0}, prepared = {0};
    check(!process_handles_initialize(&source) && process_handle_open(&source, NULL, "/DATA.BIN", FS_READ, 0) == 3,
          "prepare trusted source");
    struct rum_handle_mapping map = {3, 0, {0}};
    check(!process_handles_prepare_spawn(&prepared, &source, &map, 1, RUM_SPAWN_NO_DEFAULT_STREAMS), "prepare inherited reference");
    struct task_process process;
    check(load(&process, "parent"), "load construction rollback fixture");
    process.initial_handles = &prepared;
    struct process_handle_statistics handles = process_handles_stats();
    struct fs_statistics fs = fs_stats();
    uint32_t held = consume();
    check(!task_create_foreground_process(&process) && process_handles_count(&prepared) == 1 &&
          process_handles_stats().descriptors == handles.descriptors && fs_stats().references == fs.references,
          "failed stack construction leaves inherited table private and cwd released");
    while (held) {
        uint32_t next = *(uint32_t *)(uintptr_t)held;
        check(pmm_free_page(held), "release held page"); held = next;
    }
    check(!process_handles_destroy(&prepared) && !process_handles_destroy(&source) && paging_space_destroy(process.space),
          "producer releases failed construction");
}
static void exercise(const char *mode)
{
    bool orphan = mode[0] == 'o', cancel = mode[0] == 'b';
    check(test_file_put("DATA.BIN", "abcdef", 6), "reset bytes");
    struct fs_statistics fs = fs_stats();
    struct heap_statistics heap = heap_stats();
    uint32_t pages = pmm_stats().free_pages;
    struct task_process process;
    check(load(&process, mode) && paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
          paging_user_protect(process.space, PAGE + 8192, 1, 0), "validation pages");
    task_id parent = task_create_foreground_process(&process);
    check(parent != 0, "publish parent");
    if (cancel) {
        struct task_snapshot snapshot; bool waiting = false;
        for (unsigned i = 0; i < RUM_TASK_CAPACITY * 4u && !waiting; ++i) {
            check(task_yield() && task_snapshot_read(&snapshot), "run console waiter");
            waiting = snapshot.event_waiters && object_stats().total.references[OBJECT_OPERATION] == 1;
        }
        check(waiting && task_cancel_foreground(), "cancel retained console read");
    }
    struct task_information info;
    check(task_wait_process(parent, &info), "wait for parent");
    if (info.exit_status) { serial_writestring("rum_shared_handles_user_status: "); status(info.exit_status); serial_writestring("\n"); }
    check(info.termination == (cancel ? TASK_TERMINATION_CANCELLED : TASK_TERMINATION_EXIT) &&
          !info.exit_status && !task_handle_count(parent), "user syscall assertions");
    check(task_foreground_end(parent) && task_reap_process(parent), "collect parent");
    if (orphan) {
        struct task_snapshot snapshot;
        for (unsigned i = 0; i < RUM_TASK_CAPACITY * 3u; ++i) {
            check(task_snapshot_read(&snapshot), "orphan snapshot");
            if (!snapshot.processes) break;
            check(task_yield(), "reap adopted child");
        }
        check(test_file_remove("READY.BIN"), "remove orphan handshake");
    }
    struct process_handle_statistics handles = process_handles_stats();
    check(!handles.descriptors && !handles.descriptions && !handles.aliases && !object_stats().total.objects &&
          !object_stats().total.references[OBJECT_OPERATION] &&
          fs_stats().references == fs.references && fs_stats().objects == fs.objects &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "exit fault cancellation and failed spawn restore every ownership ledger");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && test_files_mount() && task_initialize(), "startup");
    check(test_file_put("MAP.ELF", shared_handles_asset_start, shared_handles_asset_end - shared_handles_asset_start) &&
          test_file_put("DATA.BIN", "abcdef", 6), "test-owned files");
    cpu_interrupt_enable(); pic_unmask(0);
    rollback();
    for (unsigned i = 0; i < 4; ++i) { exercise("parent"); exercise("orphan"); exercise("blocks"); }
    check(process_handles_stats().descriptors == 0 && fs_stats().references == 1, "only boot cwd remains");
    serial_writestring("rum_shared_handles_ok\n"); cpu_halt();
}
