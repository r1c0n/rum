#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "fs-backend.h"

#define PAGE 0x80800000u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char lifecycle_asset_start[], lifecycle_asset_end[];
static struct task_snapshot snapshot;
static struct rum_arguments args;
static void check(bool condition, const char *name)
{
    if (condition) return;
    serial_writestring("rum_lifecycle_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static void number(uint32_t value)
{
    char digits[11]; unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) serial_putchar(digits[--n]);
}
static struct rum_arguments arguments(const char *mode, rum_pid_t unrelated)
{
    struct rum_arguments args = {0};
    const char *values[4] = { "life", mode, "", "0" };
    char pid[11], reversed[11]; unsigned n = 0, length = 0;
    do { reversed[n++] = (char)('0' + unrelated % 10u); unrelated /= 10u; } while (unrelated);
    while (n) pid[length++] = reversed[--n];
    pid[length] = 0; values[2] = pid;
    for (unsigned i = 0; i < 4; ++i) {
        args.offsets[args.argc++] = args.string_bytes;
        const char *text = values[i];
        do { args.strings[args.string_bytes++] = *text; } while (*text++);
    }
    return args;
}
static uint32_t consume(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) { *(uint32_t *)(uintptr_t)page = head; head = page; }
    return head;
}
static void release(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "release fixture exhaustion pages"); head = next;
    }
}
static void exercise(const char *mode, bool session, bool oom, bool copy)
{
    serial_writestring("rum_lifecycle_case "); serial_writestring(mode); serial_writestring("\n");
    uint32_t pages = pmm_stats().free_pages;
    struct heap_statistics heap = heap_stats();
    struct fs_statistics fs = fs_stats();
    unsigned calls = fs_test_backend_calls();
    /* A retained, unrelated completion belongs to BOOT, not the user parent. */
    args = arguments("exit", 0);
    struct task_process other;
    check(elf_load_process(lifecycle_asset_start, lifecycle_asset_end - lifecycle_asset_start, &args, &other),
          "load unrelated child");
    task_id unrelated = task_create_process(&other);
    struct task_information info;
    check(unrelated && task_query(unrelated, &info), "publish unrelated child");
    args = arguments(mode, info.process_id);
    struct task_process process;
    check(elf_load_process(lifecycle_asset_start, lifecycle_asset_end - lifecycle_asset_start, &args, &process) &&
          paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
          paging_user_protect(process.space, PAGE + 8192, 1, 0), "load parent and cross-page buffers");
    task_id parent = task_create_foreground_process(&process);
    check(parent && (!session || task_mark_foreground_shell(parent)), "publish parent with exact shell authority");
    uint32_t held = oom ? consume() : 0;
    if (copy) {
        do { check(task_yield() && task_query(parent, &info) && info.state != TASK_EXITED, "run parent into WAIT"); }
        while (info.state != TASK_BLOCKED);
        check(paging_user_protect(process.space, PAGE, 1, 0) &&
              ramfs_put("finish.child", "ready", 5), "inject failed result copy after wait registration");
    }
    check(task_wait_process(parent, &info), "parent completion");
    if (info.exit_status) { serial_writestring("user status: "); number((uint32_t)info.exit_status); serial_writestring("\n"); }
    check(info.termination == TASK_TERMINATION_EXIT && !info.exit_status && !task_handle_count(parent),
          "ring-3 lifecycle assertions and handle teardown");
    check(task_foreground_end(parent) && task_reap_process(parent), "collect foreground parent");
    check(task_wait_process(unrelated, &info) && info.exit_status == INT32_MIN && task_reap_process(unrelated),
          "unrelated completion was never stolen");
    if (oom) release(held);
    /* Adopted children cancel on their own stacks; allow their deferred cleanup. */
    for (unsigned tries = 0; tries < RUM_TASK_CAPACITY * 3u; ++tries) {
        check(task_snapshot_read(&snapshot), "orphan ownership snapshot");
        if (!snapshot.processes) break;
        check(task_yield(), "drain adopted descendants");
    }
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.processes &&
          snapshot.created == snapshot.exited && snapshot.exited == snapshot.reaped, "all completions and orphans reclaimed");
    const char *temporary[] = { "leaf.ready", "tree.ready", "cancel.ready", "finish.child" };
    for (unsigned i = 0; i < sizeof temporary / sizeof temporary[0]; ++i) {
        const unsigned char *contents; size_t size;
        if (ramfs_read(temporary[i], &contents, &size))
            check(ramfs_remove(temporary[i]), "remove fixture handshake file");
    }
    check(fs_stats().objects == fs.objects && fs_stats().references == fs.references && !fs_test_backend_pins() &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "processes, handles, cwd references, heap and private pages return to baseline");
    if (mode[0] == 'i') check(fs_test_backend_calls() == calls, "invalid packets never reach disk backend");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && ramfs_initialize() && task_initialize(), "fixture startup");
    check(ramfs_put("life.elf", lifecycle_asset_start, lifecycle_asset_end - lifecycle_asset_start) &&
          ramfs_put("data.bin", "abc", 3), "embedded test executable and data");
    fs_test_backend_initialize(); cpu_interrupt_enable();
    check(fs_mount_disk(fs_test_backend()) == FS_OK, "directory backend");
    pic_unmask(0);
    for (unsigned i = 0; i < 2; ++i) {
        exercise("invalid", false, false, false);
        exercise("normal", false, false, false);
        exercise("session", true, false, false);
        exercise("orphan", false, false, false);
        exercise("oom", false, true, false);
        exercise("copywait", false, false, true);
    }
    check(fs_unmount_disk() == FS_OK, "mount released after descendants exit");
    serial_writestring("rum_lifecycle_ok\n"); cpu_halt();
}
