#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/keyboard.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "fs-backend.h"

#define PAGE 0x80800000u
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char process_syscall_asset_start[], process_syscall_asset_end[];
void kernel_main(uint32_t, uint32_t);
static struct task_snapshot snapshot;
static void check(bool ok, const char *name)
{
    if (!ok) { serial_writestring("rum_process_syscall_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt(); }
}
static void exercise(unsigned mode)
{
    bool cancel = mode == 1;
    uint32_t pages = pmm_stats().free_pages;
    struct heap_statistics heap = heap_stats();
    struct fs_statistics fs = fs_stats();
    struct rum_arguments args = { .argc = 2, .string_bytes = 13, .offsets = {0, 6}, .strings = "probe\0normal" };
    if (cancel) for (unsigned i = 0; i < 7; ++i) args.strings[6 + i] = "cancel"[i];
    if (mode == 2) { args.string_bytes = 14; for (unsigned i = 0; i < 8; ++i) args.strings[6 + i] = "session"[i]; }
    struct task_process process;
    check(elf_load_process(process_syscall_asset_start, process_syscall_asset_end - process_syscall_asset_start,
        &args, &process) && paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
        paging_user_protect(process.space, PAGE + 8192, 1, 0), "load parent and validation pages");
    task_id parent = task_create_foreground_process(&process);
    check(parent != 0, "publish parent");
    if (mode == 2) check(task_mark_foreground_shell(parent), "mark session supervisor");
    if (cancel) {
        bool blocked = false;
        for (unsigned tries = 0; tries < 20 && !blocked; ++tries) {
            check(task_yield() && task_snapshot_read(&snapshot), "schedule blocking grandchild");
            for (unsigned i = 0; i < snapshot.count; ++i)
                if (snapshot.tasks[i].parent == parent && snapshot.tasks[i].state == TASK_BLOCKED) blocked = true;
        }
        check(blocked && task_cancel_foreground(), "cancel only the active grandchild");
    }
    struct task_information info;
    check(task_wait_process(parent, &info), "wait for parent after nested RUN");
    if (info.exit_status) {
        char digits[12]; unsigned n = 0, value = (unsigned)info.exit_status;
        do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
        serial_writestring("user status: "); while (n) serial_putchar(digits[--n]); serial_writestring("\n");
    }
    check(info.termination == TASK_TERMINATION_EXIT && !info.exit_status && !task_handle_count(parent), "parent assertions and handle cleanup");
    check(task_foreground_end(parent) && task_reap_process(parent), "restore kernel foreground and reap");
    check(fs_stats().objects == fs.objects && fs_stats().references == fs.references, "object and cwd baseline");
    check(heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "nested exit fault and cancellation release every allocation and private page");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(keyboard_initialize() && magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
        pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
        paging_initialize() && heap_initialize() && ramfs_initialize() && task_initialize(), "fixture boot");
    check(ramfs_put("probe.elf", process_syscall_asset_start, process_syscall_asset_end - process_syscall_asset_start) &&
          ramfs_put("data.bin", "abc", 4), "fixtures");
    fs_test_backend_initialize();
    cpu_interrupt_enable();
    check(fs_mount_disk(fs_test_backend()) == FS_OK, "directory fixture mount");
    pic_unmask(0); pic_unmask(1);
    for (unsigned i = 0; i < 4; ++i) { exercise(0); exercise(1); exercise(2); }
    check(!fs_test_backend_pins() && fs_unmount_disk() == FS_OK, "backend pin baseline");
    serial_writestring("rum_process_syscall_ok\n"); cpu_halt();
}
