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
void authority_syscall_dispatch(struct exception_frame *);
#define syscall_dispatch authority_syscall_dispatch
#include "../kernel/process/syscall.c"
#undef syscall_dispatch
void syscall_dispatch(struct exception_frame *frame)
{
    if (frame && frame->eax == 0xffffffe0u && frame->vector == RUM_SYSCALL_VECTOR && !frame->error &&
        exception_frame_from_user(frame) && task_current_is_process()) {
        if (frame->ebx) {
            static const struct block_operations no_driver = {0};
            struct block_device device = { .sector_size = 512, .sector_count = 1, .online = true, .operations = &no_driver };
            unsigned char bytes[512];
            bool denied = block_read(&device, 0, 1, bytes, sizeof bytes).error == BLOCK_ACCESS &&
                block_write(&device, 0, 1, bytes, sizeof bytes).error == BLOCK_ACCESS &&
                block_flush(&device).error == BLOCK_ACCESS && !device.busy &&
                fs_mount_disk(fs_test_backend()) == FS_ACCESS && fs_unmount_disk() == FS_ACCESS;
            frame->eax = denied ? 0 : 1;
        } else frame->eax = fs_test_backend_mutation_calls();
        return;
    }
    authority_syscall_dispatch(frame);
}
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char authority_asset_start[], authority_asset_end[];
void kernel_main(uint32_t, uint32_t);
static void check(bool ok, const char *name)
{
    if (!ok) { serial_writestring("rum_system_authority_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt(); }
}
static void run(unsigned role)
{
    struct fs_statistics fs = fs_stats(); struct heap_statistics heap = heap_stats();
    uint32_t pages = pmm_stats().free_pages;
    struct process_handles source = {0}, initial = {0}; struct object_handles objects = {0};
    check(!process_handles_initialize(&source) && process_handle_open(&source, NULL, "/RUM/CORE.BIN", 3, 0) == 3, "trusted writable open");
    rum_object_handle_t token;
    check(!process_handle_export(&source, 3, &objects, RUM_OBJECT_FILE, RUM_RIGHT_FILE, RUM_OBJECT_INHERIT, &token), "trusted write grant");
    struct rum_handle_mapping map = {3, 8, {0}};
    check(!process_handles_prepare_spawn(&initial, &source, &map, 1, 0), "prepare inherited write descriptor");
    struct rum_arguments args = { .argc = 3 };
    const char *values[] = {"/RUM/AUTH.ELF", role == 0 ? "shell" : role == 1 ? "app" : "io"};
    for (unsigned i = 0; i < 2; ++i) {
        args.offsets[i] = args.string_bytes;
        for (unsigned j = 0;; ++j) { args.strings[args.string_bytes++] = values[i][j]; if (!values[i][j]) break; }
    }
    args.offsets[2] = args.string_bytes;
    static const char hex[] = "0123456789abcdef";
    for (unsigned j = 0; j < 8; ++j) args.strings[args.string_bytes++] = hex[(token >> (28 - j * 4)) & 15];
    ++args.string_bytes;
    struct task_process process;
    check(elf_load_process(authority_asset_start, authority_asset_end - authority_asset_start, &args, &process), "load public-only fixture");
    memcpy(process.name, role == 0 ? "ORDINARY.ELF" : "SHELL.ELF", role == 0 ? 13 : 10);
    process.initial_handles = &initial; process.initial_objects = &objects;
    task_id id = role == 0 ? task_create_shell_process(&process) : task_create_foreground_process(&process);
    check(id && !process_handles_destroy(&source), "publish explicit role and release producer entries");
    struct task_information info;
    check(task_wait_process(id, &info), "wait user authority checks");
    if (info.exit_status) {
        serial_writestring("user status: "); unsigned count = 0; char digits[12]; uint32_t status = (uint32_t)info.exit_status;
        do { digits[count++] = (char)('0' + status % 10); status /= 10; } while (status);
        while (count) serial_putchar(digits[--count]);
        serial_writestring("\n");
    }
    check(role == 2 ? info.termination == TASK_TERMINATION_FAULT && info.fault.vector == 13 :
          info.termination == TASK_TERMINATION_EXIT && !info.exit_status, "roles, identity protection and user CPU restrictions");
    check(task_foreground_end(id) && task_reap_process(id), "collect process");
    check(!object_stats().total.objects && !process_handles_stats().descriptors &&
          fs_stats().references == fs.references && fs_stats().objects == fs.objects &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          (uint64_t)pmm_stats().free_pages + heap_stats().mapped_bytes / PAGE_SIZE ==
          (uint64_t)pages + heap.mapped_bytes / PAGE_SIZE, "authority denial/fault restores ownership ledgers");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && test_files_mount() && task_initialize(), "startup");
    cpu_interrupt_enable(); pic_unmask(0);
    check(fs_mkdir(NULL, "/RUM") == FS_OK && fs_mkdir(NULL, "/RUM/SUB") == FS_OK &&
          fs_replace(NULL, "/RUM/CORE.BIN", "trusted", 7) == FS_OK &&
          fs_replace(NULL, "/RUM/SUB/CFG", "cfg", 3) == FS_OK &&
          fs_replace(NULL, "/RUM/AUTH.ELF", authority_asset_start, authority_asset_end - authority_asset_start) == FS_OK &&
          fs_test_backend_alias("SYS", "/RUM") && fs_test_backend_alias("CORE.ALS", "/RUM/CORE.BIN"), "prepare system identities and aliases");
    for (unsigned i = 0; i < 3; ++i) { run(0); run(1); run(2); }
    serial_writestring("rum_system_authority_ok\n"); cpu_halt();
}
