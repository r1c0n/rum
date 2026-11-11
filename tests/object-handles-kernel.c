#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/handles.h>
#include <rum/memory.h>
#include <rum/object_handle.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "test-files.h"

extern const char __kernel_start[], __kernel_end[];
extern const unsigned char object_handles_asset_start[], object_handles_asset_end[];
void kernel_main(uint32_t magic, uint32_t information);
static unsigned destroyed;
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void check(bool ok, const char *name)
{
    if (!ok) { serial_writestring("rum_object_handles_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt(); }
}
static rum_result_t destroy(struct kernel_object *object)
{
    check(task_blocking_allowed(), "destructor context");
    ++destroyed; check(kfree(object), "free typed object"); return 0;
}
static const struct object_type file = { OBJECT_FILE, destroy };
static const struct object_type console = { OBJECT_CONSOLE_OUTPUT, destroy };
static const struct object_type process_type = { OBJECT_PROCESS, destroy };
static rum_object_handle_t give(struct object_handles *table, const struct object_type *type, uint32_t rights, unsigned flags)
{
    struct kernel_object *object = kcalloc(1, sizeof *object);
    rum_object_handle_t handle = 0;
    check(object && !object_initialize(object, type) && !object_publish(object, OBJECT_OWNER), "construct typed fixture");
    check(!object_handle_install(table, object, object_rights_mask(type->kind), rights, flags, &handle), "grant authorized rights");
    return handle;
}
static struct rum_arguments arguments(const char *mode, const rum_object_handle_t handles[3])
{
    struct rum_arguments args = { .argc = 5 };
    const char *text[] = {"/CAP.ELF", mode};
    for (unsigned i = 0; i < 2; ++i) {
        args.offsets[i] = args.string_bytes;
        size_t bytes = 1; while (text[i][bytes - 1]) ++bytes;
        memcpy(args.strings + args.string_bytes, text[i], bytes); args.string_bytes += bytes;
    }
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 3; ++i) {
        args.offsets[i + 2] = args.string_bytes;
        for (unsigned j = 0; j < 8; ++j) args.strings[args.string_bytes++] = hex[(handles[i] >> (28 - j * 4)) & 15];
        ++args.string_bytes;
    }
    return args;
}
static uint32_t consume(void)
{
    uint32_t head = 0, page;
    while ((page = pmm_allocate_page())) { *(uint32_t *)(uintptr_t)page = head; head = page; }
    return head;
}
static void run(const char *mode, enum task_termination expected)
{
    struct object_handles table = {0};
    rum_object_handle_t handles[3] = {0};
    bool wrap = equal(mode, "wrap"), rollback = equal(mode, "rollback");
    if (wrap) {
        for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT; ++i) table.entries[i].generation = OBJECT_HANDLE_GENERATION_MAX;
        --table.entries[31].generation;
    }
    unsigned baseline = destroyed;
    handles[0] = give(&table, &file, RUM_RIGHT_COMMON | RUM_RIGHT_READ, RUM_OBJECT_INHERIT);
    if (!wrap) {
        handles[1] = give(&table, &console, RUM_RIGHT_QUERY, 0);
        handles[2] = give(&table, &process_type, RUM_RIGHT_QUERY | RUM_RIGHT_WAIT, 0);
    }
    struct rum_arguments args = arguments(mode, handles);
    struct task_process process;
    check(elf_load_process(object_handles_asset_start, object_handles_asset_end - object_handles_asset_start, &args, &process),
          "load independently built user fixture");
    process.initial_objects = &table;
    if (rollback) {
        uint32_t held = consume();
        check(!task_create_foreground_process(&process) && object_handles_count(&table) == 3 &&
              destroyed == baseline, "failed construction leaves producer ownership unchanged");
        while (held) { uint32_t next = *(uint32_t *)(uintptr_t)held; check(pmm_free_page(held), "restore held page"); held = next; }
        check(!object_handles_destroy(&table) && paging_space_destroy(process.space), "release failed construction resources");
    } else {
        task_id id = task_create_foreground_process(&process);
        check(id && !object_handles_count(&table), "move complete object table at publication");
        if (expected == TASK_TERMINATION_CANCELLED) {
            uint32_t deadline = timer_ticks() + 400;
            while (object_stats().total.references[OBJECT_TABLE] < 4) {
                check(!timer_deadline_reached(timer_ticks(), deadline) && task_yield(), "wait for CPU-bound alias owner");
            }
            check(task_cancel_foreground(), "cancel CPU-bound typed-handle owner");
        }
        struct task_information info;
        check(task_wait_process(id, &info), "wait typed-handle process");
        if (info.termination != expected || (expected == TASK_TERMINATION_EXIT && info.exit_status)) {
            serial_writestring("mode: "); serial_writestring(mode);
            serial_writestring(" status: ");
            char digits[12]; unsigned n = 0; uint32_t status = (uint32_t)info.exit_status;
            do { digits[n++] = (char)('0' + status % 10); status /= 10; } while (status);
            while (n) serial_putchar(digits[--n]);
            serial_writestring("\n");
            check(false, "user status");
        }
        check(task_foreground_end(id) && task_reap_process(id), "reap typed-handle process");
    }
    struct object_statistics stats = object_stats();
    check(destroyed == baseline + (wrap ? 1u : 3u) && !stats.total.objects &&
          !stats.total.references[OBJECT_TABLE] && !stats.total.references[OBJECT_OPERATION] &&
          !process_handles_stats().descriptors && !paging_stats().user_pages, "teardown restores object/descriptor/page counts");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && test_files_mount() && task_initialize(), "startup");
    cpu_interrupt_enable(); timer_initialize(); pic_unmask(0);
    check(test_file_put("CAP.ELF", object_handles_asset_start, object_handles_asset_end - object_handles_asset_start), "install user fixture");
    struct heap_statistics heap = heap_stats();
    uint32_t pages = pmm_stats().free_pages;
    for (unsigned i = 0; i < 3; ++i) {
        run("parent", TASK_TERMINATION_EXIT); run("fault", TASK_TERMINATION_FAULT);
        run("spin", TASK_TERMINATION_CANCELLED); run("wrap", TASK_TERMINATION_EXIT);
        run("rollback", TASK_TERMINATION_EXIT);
    }
    check(heap_stats().used_bytes == heap.used_bytes && heap_stats().allocations == heap.allocations &&
          (uint64_t)pmm_stats().free_pages + heap_stats().mapped_bytes / PAGE_SIZE ==
          (uint64_t)pages + heap.mapped_bytes / PAGE_SIZE, "repeated ownership cycles restore heap and physical capacity");
    serial_writestring("rum_object_handles_ok\n"); cpu_halt();
}
