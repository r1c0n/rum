#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/fs.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include "fs-backend.h"
#include "fs-checks.h"

extern const char __kernel_start[], __kernel_end[];
extern const unsigned char fs_hello_start[], fs_hello_end[], fs_fault_start[], fs_fault_end[], fs_spin_start[], fs_spin_end[];
void kernel_main(uint32_t magic, uint32_t information);

void fs_check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_fs_test_failed: "); serial_writestring(name);
        serial_writestring("\n"); cpu_halt();
    }
}
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void worker(void *argument)
{
    (void)argument;
    struct fs_context *cwd = task_current_filesystem();
    fs_check(cwd && equal(cwd->path, "/disk/DOCS") && fs_context_chdir(cwd, "..") == FS_OK &&
             equal(cwd->path, "/disk"), "worker inherits and changes its own cwd");
}
static uint32_t consume(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) { *(uint32_t *)(uintptr_t)page = head; head = page; }
    return head;
}
static void release_pages(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        fs_check(pmm_free_page(head), "release OOM page"); head = next;
    }
}
static const struct rum_arguments arguments = {
    .argc = 1, .string_bytes = 10, .strings = "hello.elf",
};
static void process_test(const char *name, enum task_termination termination, bool cancel)
{
    struct task_process process;
    struct task_information info;
    struct fs_context *cwd = task_current_filesystem();
    fs_check(fs_context_chdir(cwd, "/disk/DOCS") == FS_OK, "set creator process cwd");
    unsigned references = fs_stats().references;
    uint32_t pages = pmm_stats().free_pages;
    fs_check(elf_load_ramfs(name, &arguments, &process), "load actual embedded user program");
    /* Stack construction OOM must not publish a cwd or consume caller space. */
    uint32_t held = consume();
    fs_check(!task_create_foreground_process(&process) && fs_stats().references == references &&
             paging_directory_address(process.space), "failed process construction releases cloned cwd");
    release_pages(held);
    task_id id = task_create_foreground_process(&process);
    char path[FS_PATH_CAPACITY];
    fs_check(id && task_working_directory(id, path, sizeof path) && equal(path, "/disk/DOCS") &&
             !task_working_directory(id, path, 4), "user process inherits bounded cwd");
    fs_check(fs_context_chdir(cwd, "/") == FS_OK && fs_unmount_disk() == FS_BUSY,
             "child pins mounted cwd after parent leaves");
    if (cancel) fs_check(task_cancel_foreground(), "request process cancellation");
    fs_check(task_wait_process(id, &info) && info.termination == termination &&
             task_working_directory(id, path, sizeof path) && equal(path, "/disk/DOCS") &&
             fs_unmount_disk() == FS_BUSY, "exited process retains cwd until parent reaps");
    fs_check(task_foreground_end(id) && task_reap_process(id) && !task_working_directory(id, path, sizeof path) &&
             fs_stats().references == references && !fs_test_backend_pins() && pmm_stats().free_pages == pages,
             "exit fault cancellation restore cwd and page ledgers");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize();
    fs_check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
             pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
             paging_initialize() && heap_initialize() && ramfs_initialize() && embedded_files_install() &&
             task_initialize(), "filesystem kernel setup");
    cpu_interrupt_enable();
    fs_check(ramfs_put("hello.elf", fs_hello_start, fs_hello_end - fs_hello_start) &&
             ramfs_put("fault.elf", fs_fault_start, fs_fault_end - fs_fault_start) &&
             ramfs_put("spin.elf", fs_spin_start, fs_spin_end - fs_spin_start), "install private ELF fixtures");
    fs_checks();
    /* Verify helpers preserve the caller's IF state even on rejected calls. */
    uint32_t flags = cpu_interrupt_save();
    fs_reference ref;
    fs_check(fs_open(NULL, "/", FS_READ, &ref) == FS_OK && fs_close(ref) == FS_OK &&
             !(cpu_interrupt_save() & 0x200), "reference operations preserve IF clear");
    cpu_interrupt_restore(flags);
    fs_check((cpu_interrupt_save() & 0x200) != 0, "reference operations preserve IF set");
    cpu_interrupt_restore(flags);
    fs_test_backend_initialize();
    fs_check(fs_mount_disk(fs_test_backend()) == FS_OK, "mount for process cwd checks");
    struct fs_context *cwd = task_current_filesystem();
    fs_check(cwd && fs_context_chdir(cwd, "/disk/DOCS") == FS_OK, "boot task cwd");
    unsigned references = fs_stats().references;
    uint32_t held = consume();
    fs_check(!task_create(worker, NULL, NULL) && fs_stats().references == references,
             "failed worker construction releases cwd");
    release_pages(held);
    task_id id = task_create(worker, NULL, NULL);
    fs_check(id && task_yield() && equal(cwd->path, "/disk/DOCS"), "worker cwd does not change parent");
    (void)task_reap();
    fs_check(fs_stats().references == references, "worker reaper closes directory reference");
    fs_check(fs_context_chdir(cwd, "/") == FS_OK, "return boot cwd to root");
    for (unsigned i = 0; i < 3; ++i) {
        process_test("hello.elf", TASK_TERMINATION_EXIT, false);
        process_test("fault.elf", TASK_TERMINATION_FAULT, false);
        process_test("spin.elf", TASK_TERMINATION_CANCELLED, true);
    }
    fs_check(fs_unmount_disk() == FS_OK && fs_stats().references == 1 && fs_stats().objects == 1,
             "only boot root reference remains");
    terminal_clear(); terminal_writestring("rum filesystem and working directory tests passed.\n");
    serial_writestring("rum_fs_test_ok\n"); cpu_halt();
}
