#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/pipe.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/terminal.h>
#include "test-files.h"
/* Test-only ID reuse verifies references bind to objects, even if the current
   production policy of monotonic, non-wrapping IDs is changed later. */
#include "../kernel/process/task.c"
void process_objects_syscall_dispatch(struct exception_frame *);
#define syscall_dispatch process_objects_syscall_dispatch
#include "../kernel/process/syscall.c"
#undef syscall_dispatch
#define TEST_QUERY 0xffffffe0u
#define PAGE 0x80800000u
static void check(bool, const char *);
static struct task_process load(const char *, rum_pid_t);
void syscall_dispatch(struct exception_frame *frame)
{
    if (frame && frame->eax == TEST_QUERY && frame->vector == RUM_SYSCALL_VECTOR && !frame->error &&
        exception_frame_from_user(frame) && task_current_is_process()) {
        unsigned operation = frame->ebx;
        if (operation == 0) frame->eax = pipe_stats().read_waiters;
        else if (operation == 1) frame->eax = object_stats().total.references[OBJECT_OPERATION];
        else if (operation == 3) frame->eax = object_stats().kinds[OBJECT_PROCESS].objects;
        else if (operation == 5) frame->eax = paging_stats().user_pages;
        else if (operation == 4) {
            cpu_interrupt_enable();
            struct task_process process = load("exit", 0);
            struct object_statistics before = object_stats();
            uint32_t held = 0, page;
            while ((page = pmm_allocate_page())) { *(uint32_t *)(uintptr_t)page = held; held = page; }
            check(!task_create_process(&process), "inherited process construction must fail without stack pages");
            while (held) { uint32_t next = *(uint32_t *)(uintptr_t)held; check(pmm_free_page(held), "restore page"); held = next; }
            struct object_statistics after = object_stats();
            check(!memcmp(&before, &after, sizeof before) && paging_space_destroy(process.space),
                  "failed child construction rolls back inherited process references");
            frame->eax = 1;
        }
        else if (operation == 2) {
            frame->eax = 0;
            for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i) {
                struct process_reference *reference = process_reference_from_object(current->process->objects.entries[i].object);
                if (reference && !reference->attached && reference->information.process_id == frame->ecx) {
                    next_id = reference->execution_id; next_process_id = reference->information.process_id;
                    frame->eax = next_process_id; break;
                }
            }
        } else frame->eax = (uint32_t)-RUM_EINVAL;
        return;
    }
    process_objects_syscall_dispatch(frame);
}
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char process_objects_asset_start[], process_objects_asset_end[];
void kernel_main(uint32_t, uint32_t);
static void check(bool ok, const char *message)
{
    if (!ok) { serial_writestring("rum_process_objects_failed: "); serial_writestring(message); serial_writestring("\n"); cpu_halt(); }
}
static struct task_process load(const char *mode, rum_pid_t foreign)
{
    struct rum_arguments args = { .argc = 3, .offsets = {0, 10}, .string_bytes = 10, .strings = "/PROC.ELF" };
    unsigned length = 0;
    do { args.strings[args.string_bytes++] = mode[length]; } while (mode[length++]);
    args.offsets[2] = args.string_bytes;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) args.strings[args.string_bytes++] = hex[(foreign >> (28 - 4 * i)) & 15];
    ++args.string_bytes;
    struct task_process process;
    check(elf_load_process(process_objects_asset_start, process_objects_asset_end - process_objects_asset_start, &args, &process), "load fixture");
    return process;
}
static void run(bool cancel)
{
    struct heap_statistics heap = heap_stats();
    uint32_t pages = pmm_stats().free_pages, references = fs_stats().references;
    struct task_process foreign_process = load("exit", 0);
    task_id foreign = task_create_process(&foreign_process);
    struct task_information information;
    check(foreign && task_query(foreign, &information), "publish unrelated process");
    struct task_process process = load(cancel ? "cancel" : "parent", information.process_id);
    check(paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
          paging_user_protect(process.space, PAGE + 8192, 1, 0), "cross-page validation regions");
    task_id parent = task_create_foreground_process(&process); check(parent, "publish parent");
    if (cancel) {
        uint32_t deadline = timer_ticks() + 400;
        while (!object_stats().total.references[OBJECT_OPERATION])
            check(!timer_deadline_reached(timer_ticks(), deadline) && task_yield(), "run retained process wait");
        check(task_cancel_foreground(), "cancel parent blocked on process object");
    }
    check(task_wait_process(parent, &information), "parent completion");
    if (information.exit_status) {
        serial_writestring("user status: ");
        char digits[12]; unsigned count = 0; uint32_t status = (uint32_t)information.exit_status;
        do { digits[count++] = (char)('0' + status % 10); status /= 10; } while (status);
        while (count) serial_putchar(digits[--count]);
        serial_writestring("\n");
    }
    check(information.termination == (cancel ? TASK_TERMINATION_CANCELLED : TASK_TERMINATION_EXIT) &&
          !information.exit_status && task_foreground_end(parent) && task_reap_process(parent), "user assertions and parent cleanup");
    check(task_wait_process(foreign, &information) && information.exit_status == 17 && task_reap_process(foreign), "foreign result preserved");
    struct task_snapshot snapshot;
    for (unsigned i = 0; i < 100; ++i) {
        check(task_snapshot_read(&snapshot), "read registry");
        if (!snapshot.processes) break;
        check(task_yield(), "reap cancelled descendants");
    }
    process_references_reclaim();
    check(!snapshot.processes && !object_stats().total.objects && !pipe_stats().pipes &&
          !process_handles_stats().descriptors && fs_stats().references == references &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          (uint64_t)pmm_stats().free_pages + heap_stats().mapped_bytes / PAGE_SIZE ==
          (uint64_t)pages + heap.mapped_bytes / PAGE_SIZE, "all process, object, I/O, heap and page owners released");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && test_files_mount() && task_initialize(), "startup");
    cpu_interrupt_enable(); pic_unmask(0);
    check(test_file_put("PROC.ELF", process_objects_asset_start, process_objects_asset_end - process_objects_asset_start), "install fixture");
    for (unsigned i = 0; i < 3; ++i) { run(false); run(true); }
    serial_writestring("rum_process_objects_ok\n"); cpu_halt();
}
