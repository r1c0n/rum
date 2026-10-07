#include <rum/abi/error.h>
#include <rum/ata.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/fat16.h>
#include <rum/handles.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/keyboard.h>
#include <rum/memory.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "fs-backend.h"

#define PAGE 0x80800000u
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char file_syscall_asset_start[], file_syscall_asset_end[];
void kernel_main(uint32_t, uint32_t);
static unsigned char pattern[1537];
static struct process_handles guard_table;
static bool irq_guarded;
static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_file_syscall_failed: "); serial_writestring(name);
        serial_writestring("\n"); cpu_halt();
    }
}
static void number(uint32_t value)
{
    char buffer[11]; unsigned n = 0;
    do { buffer[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) serial_putchar(buffer[--n]);
}
static void guarded_irq(void)
{
    irq_guarded = process_handle_open(&guard_table, NULL, "/data.bin", 1, 0) == -RUM_EBUSY;
}
static void guards(void)
{
    process_handles_initialize(&guard_table);
    unsigned calls = fs_test_backend_calls();
    uint32_t flags = cpu_interrupt_save();
    check(process_handle_open(&guard_table, NULL, "/disk/DOCS", 1, 0) == -RUM_EBUSY &&
          fs_test_backend_calls() == calls, "IF-clear requests never call a backend");
    cpu_interrupt_restore(flags);
    irq_register(0, guarded_irq);
    irq_dispatch(&(struct exception_frame){ .vector = PIC_VECTOR_BASE });
    check(irq_guarded && fs_test_backend_calls() == calls && process_handles_destroy(&guard_table) == 0,
          "IRQ requests never call a backend");
    timer_initialize();
}
static uint32_t consume(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) { *(uint32_t *)(uintptr_t)page = head; head = page; }
    return head;
}
static void release_pages(uint32_t head)
{
    while (head) { uint32_t next = *(uint32_t *)(uintptr_t)head; check(pmm_free_page(head), "release OOM pages"); head = next; }
}
static void exercise(char mode, bool construction)
{
    bool fault = mode == 'f' || mode == 'F', cancel = mode == 'c' || mode == 'C';
    check(ramfs_put("data.bin", pattern, sizeof pattern), "reset RAM fixture");
    struct fs_statistics baseline = fs_stats();
    uint32_t pages = pmm_stats().free_pages;
    size_t heap_bytes = heap_stats().used_bytes;
    size_t heap_mapped = heap_stats().mapped_bytes;
    struct rum_arguments arguments = { .argc = 2, .string_bytes = 7, .offsets = {0, 5}, .strings = "test\0n" };
    arguments.strings[5] = mode;
    struct task_process process;
    check(elf_load_process(file_syscall_asset_start, (size_t)(file_syscall_asset_end - file_syscall_asset_start),
                          &arguments, &process), "load file-syscall userspace fixture");
    check(paging_user_allocate(process.space, PAGE, 3, PAGING_WRITABLE) &&
          paging_user_protect(process.space, PAGE + 2 * RUM_PAGE_SIZE, 1, 0), "cross-page and protected buffer mappings");
    if (construction) {
        struct fs_statistics before = fs_stats();
        uint32_t held = consume();
        check(!task_create_foreground_process(&process) && fs_stats().references == before.references &&
              fs_stats().objects == before.objects, "partial construction leaves handle and object ledgers intact");
        release_pages(held);
    }
    task_id id = task_create_foreground_process(&process);
    check(id && task_handle_count(id) == 3, "process starts with exactly three streams");
    if (cancel) {
        struct task_information waiting;
        do { check(task_yield() && task_query(id, &waiting), "schedule cancellation fixture"); }
        while (waiting.state != TASK_BLOCKED && waiting.state != TASK_EXITED);
        check(waiting.state == TASK_BLOCKED && task_handle_count(id) == 5 &&
              fs_remove(NULL, "/data.bin") == FS_BUSY && fs_replace(NULL, "/data.bin", NULL, 0) == FS_BUSY,
              "sleeping child retains file identity across removal and replacement");
        if (mode == 'C')
            check(fs_remove(NULL, "/disk/DOCS/NOTE.TXT") == FS_BUSY &&
                  fs_replace(NULL, "/disk/DOCS/NOTE.TXT", NULL, 0) == FS_BUSY &&
                  fs_remove(NULL, "/disk/DOCS") == FS_BUSY,
                  "sleeping child retains real disk handles and cwd after ATA I/O");
        check(task_cancel_foreground(), "cancel child holding file");
    }
    struct task_information info;
    check(task_wait_process(id, &info), "wait for filesystem child");
    if (info.termination == TASK_TERMINATION_EXIT && info.exit_status) {
        serial_writestring("rum_file_syscall_user_error: "); number((uint32_t)info.exit_status); serial_writestring("\n");
    }
    check(info.termination == (fault ? TASK_TERMINATION_FAULT : cancel ? TASK_TERMINATION_CANCELLED : TASK_TERMINATION_EXIT) &&
          !info.exit_status && task_handle_count(id) == 0 && fs_stats().references == baseline.references + 1,
          "exit fault and cancellation close every handle immediately");
    check(task_foreground_end(id) && task_reap_process(id), "reap child");
    struct fs_statistics after = fs_stats();
    check(after.references == baseline.references && after.objects == baseline.objects,
          "repeated process exit restores file object and reference ledgers");
    /* Kernel heap pages remain mapped for reuse; FAT mutation can grow this
       shared pool, independently of the process's owned address space. */
    check(pmm_stats().free_pages + (heap_stats().mapped_bytes - heap_mapped) / RUM_PAGE_SIZE == pages,
          "process exit returns all private pages after accounting for heap pool growth");
    check(heap_stats().used_bytes == heap_bytes, "process exit restores live heap allocation bytes");
    if (mode == 'n') {
        const unsigned char *data; size_t bytes;
        check(ramfs_read("data.bin", &data, &bytes) && bytes == sizeof pattern, "RAM file retains size");
        for (unsigned i = 0; i < bytes; ++i)
            check(data[i] == (i < 700 ? (unsigned char)(i ^ 0xa5) : pattern[i]), "exact RAM bytes and write canaries");
    }
    serial_writestring("rum_file_syscall_case_ok: "); serial_putchar(mode); serial_writestring("\n");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(keyboard_initialize() && magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && ramfs_initialize() && task_initialize(), "filesystem syscall fixture boot");
    for (unsigned i = 0; i < sizeof pattern; ++i) pattern[i] = (unsigned char)(i * 17 + 3);
    cpu_interrupt_enable();
    const struct multiboot_info *boot = (const void *)(uintptr_t)information;
    char mode = 'n';
    if ((boot->flags & 4) && boot->cmdline) {
        const char *command = (const void *)(uintptr_t)boot->cmdline;
        while (*command) { if (command[0] == ' ' && (command[1] == 'd' || command[1] == 'r')) mode = command[1]; ++command; }
    }
    if (mode == 'd' || mode == 'r') {
        check(ata_initialize().error == BLOCK_OK, "syscall fixture ATA disk");
        check((mode == 'd' ? fat16_mount(ata_device()) : fat16_mount_read_only(ata_device())) == FS_OK, "syscall fixture FAT mount");
        exercise(mode, false);
        if (mode == 'd') {
            pic_unmask(0); pic_unmask(1);
            for (unsigned i = 0; i < 4; ++i) { exercise('F', false); exercise('C', false); }
        }
        check(fat16_unmount() == FS_OK, "disk handles and cwd references were released");
    } else {
        fs_test_backend_initialize();
        check(fs_mount_disk(fs_test_backend()) == FS_OK &&
              fs_replace(NULL, "/disk/DOCS/NOTE.TXT", "short operations", 16) == FS_OK, "short-I/O backend fixture");
        guards(); pic_unmask(0); pic_unmask(1);
        for (unsigned i = 0; i < 4; ++i) { exercise('n', i == 0); exercise('f', false); exercise('c', false); }
        fs_test_backend_short = 7; fs_test_backend_partial_error = FS_IO_ERROR;
        exercise('s', false);
        fs_test_backend_short = 0; fs_test_backend_partial_error = FS_OK;
        check(!fs_test_backend_pins() && fs_unmount_disk() == FS_OK, "mock backend pin baseline");
    }
    serial_writestring("rum_file_syscall_ok\n"); cpu_halt();
}
