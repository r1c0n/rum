#include <rum/abi/syscall.h>
#include <rum/abi/error.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/handles.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/process.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "test-files.h"
#include "fs-backend.h"

#define INJECT 0xffffffc0u
#define READY 0xffffffc1u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char large_loader_asset_start[], large_loader_asset_end[];
static struct fs_operations operations;
static struct fs_backend backend;
static unsigned reads, normal_reads, injection, fail_at;
static uint32_t held;
static bool ready;
struct baseline {
    struct paging_statistics memory;
    struct heap_statistics heap;
    struct process_handle_statistics handles;
    struct fs_statistics files;
    uint32_t pages;
};
static struct baseline parent_baseline;
static void check(bool ok, const char *name)
{
    if (ok) return;
    serial_writestring("rum_large_loader_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static struct baseline baseline(void)
{
    return (struct baseline){ paging_stats(), heap_stats(), process_handles_stats(), fs_stats(), pmm_stats().free_pages };
}
static void unchanged(struct baseline before)
{
    struct baseline after = baseline();
    check(after.memory.spaces == before.memory.spaces && after.memory.user_pages == before.memory.user_pages &&
          after.memory.private_table_pages == before.memory.private_table_pages && after.memory.regions == before.memory.regions &&
          after.memory.metadata_bytes == before.memory.metadata_bytes && after.memory.buffer_users == before.memory.buffer_users &&
          after.handles.descriptions == before.handles.descriptions && after.handles.descriptors == before.handles.descriptors &&
          after.files.objects == before.files.objects && after.files.references == before.files.references &&
          after.heap.used_bytes == before.heap.used_bytes && after.heap.allocations == before.heap.allocations &&
          after.pages + (after.heap.mapped_bytes - before.heap.mapped_bytes) / RUM_ABI_PAGE_SIZE == before.pages,
          "page, region, descriptor, filesystem and metadata counts return to baseline");
}
static uint32_t consume(uint32_t keep)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages > keep) {
        uint32_t page = pmm_allocate_page(); check(page != 0, "hold available physical frames");
        *(uint32_t *)(uintptr_t)page = head; head = page;
    }
    return head;
}
static void release(uint32_t head)
{
    while (head) { uint32_t next = *(uint32_t *)(uintptr_t)head; check(pmm_free_page(head), "release held frame"); head = next; }
}
static struct fs_io_result read_file(void *context, uint64_t id, uint64_t offset, void *out, size_t bytes)
{
    check(bytes && bytes <= ELF_READ_CHUNK, "launcher reads are bounded independently of image size");
    ++reads;
    struct fs_io_result result = fs_test_backend()->operations->read(context, id, offset, out, bytes);
    if (injection && reads == fail_at) {
        if (injection == 3) result.error = FS_IO_ERROR; /* Error after a transferred prefix. */
        else if (injection == 4) result = (struct fs_io_result){0};
        else if (injection == 5) result.transferred = bytes + 1;
        else result = (struct fs_io_result){ .error = injection == 2 ? FS_TIMEOUT : FS_IO_ERROR };
    }
    return result;
}
void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) { irq_dispatch(frame); return; }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax == READY) { ready = true; frame->eax = 0; return; }
    if (frame->eax != INJECT) { syscall_dispatch(frame); return; }
    cpu_interrupt_enable();
    if (!frame->ebx) {
        if (held) { release(held); held = 0; }
        injection = 0; unchanged(parent_baseline);
    } else {
        parent_baseline = baseline(); reads = 0; injection = frame->ebx;
        const unsigned stages[] = {0, 1, 2, 3, 23, normal_reads};
        check(injection <= 6, "valid injection selector");
        fail_at = injection < 6 ? stages[injection] : 0;
        if (injection == 6) held = consume(0);
    }
    frame->eax = 0;
}
static struct rum_arguments arguments(char mode)
{
    struct rum_arguments value = { .argc = 2, .string_bytes = 12, .offsets = {0, 10}, .strings = "LARGE.ELF\0n" };
    value.strings[10] = mode; return value;
}
static void run(char mode)
{
    serial_writestring("rum_large_loader_case "); serial_putchar(mode); serial_writestring("\n");
    struct baseline before = baseline();
    struct rum_arguments args = arguments(mode); task_id child = 0;
    reads = 0; ready = false;
    check(!process_start_foreground("/LARGE.ELF", &args, &child) && child, "stream real executable from common filesystem");
    if (!normal_reads) normal_reads = reads;
    if (mode == 's') {
        uint32_t start = timer_ticks();
        while (!ready) check(timer_ticks() - start < 300 && task_yield(), "reach CPU-bound child");
        check(task_cancel_foreground(), "cancel child with large mappings and live heap");
    }
    struct task_information info;
    check(task_wait_process(child, &info), "wait for large process");
    if (info.exit_status) { serial_writestring("user failure code: "); serial_putchar((char)('0' + info.exit_status / 10));
        serial_putchar((char)('0' + info.exit_status % 10)); serial_writestring("\n"); }
    check(mode == 'f' || mode == 'g' ? info.termination == TASK_TERMINATION_FAULT && info.fault.vector == 14 &&
          (mode == 'g' ? info.fault.address == RUM_ABI_STACK_BASE - 1 && info.fault.error == 6 : info.fault.error == 7) :
          mode == 's' ? info.termination == TASK_TERMINATION_CANCELLED :
          info.termination == TASK_TERMINATION_EXIT && !info.exit_status, "expected large process result");
    check(task_foreground_end(child) && task_reap_process(child), "reap large process and restore recovery context");
    unchanged(before);
}
static void allocation_failures(void)
{
    struct rum_arguments args = arguments('n');
    struct task_process measure; uint32_t free = pmm_stats().free_pages;
    check(elf_load_process(large_loader_asset_start, large_loader_asset_end - large_loader_asset_start, &args, &measure), "measure large mapping cost");
    unsigned required = free - pmm_stats().free_pages;
    check(required > 160 && paging_space_destroy(measure.space), "release measured mappings");
    const unsigned keeps[] = {0, 1, 2, 8, 32, 70, 160, required - 1};
    for (unsigned i = 0; i < sizeof keeps / sizeof keeps[0]; ++i) {
        struct baseline before = baseline(); uint32_t head = consume(keeps[i]); task_id child = 0;
        check(process_start_foreground("/LARGE.ELF", &args, &child) == -RUM_ENOMEM && !child,
              "large mapping construction reports recoverable exhaustion");
        release(head); unchanged(before);
    }
}
static void concurrent(void)
{
    struct baseline before = baseline(); struct task_process processes[2]; task_id children[2];
    struct rum_arguments args = arguments('n');
    for (unsigned i = 0; i < 2; ++i)
        check(elf_load_process(large_loader_asset_start, large_loader_asset_end - large_loader_asset_start, &args, &processes[i]),
              "prepare concurrent large processes");
    struct rum_elf_header header; memcpy(&header, large_loader_asset_start, sizeof header);
    for (unsigned i = 0; i < header.phnum; ++i) {
        struct rum_elf_program program;
        memcpy(&program, large_loader_asset_start + header.phoff + i * sizeof program, sizeof program);
        if (program.type != RUM_ELF_PT_LOAD || !program.memory_bytes) continue;
        uint32_t left, right;
        check(paging_translate(processes[0].space, program.address, &left) &&
              paging_translate(processes[1].space, program.address, &right) && left != right,
              "large LOAD segments have private physical frames");
    }
    for (unsigned i = 0; i < 2; ++i) { children[i] = task_create_process(&processes[i]); check(children[i] != 0, "publish concurrent image"); }
    for (unsigned i = 0; i < 2; ++i) {
        struct task_information info;
        check(task_wait_process(children[i], &info) && info.termination == TASK_TERMINATION_EXIT && !info.exit_status &&
              task_reap_process(children[i]), "concurrent large workloads exit cleanly");
    }
    unchanged(before);
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize(), "fixture startup");
    operations = *fs_test_backend()->operations; operations.read = read_file;
    backend = *fs_test_backend(); backend.operations = &operations;
    check(fs_mount_disk(&backend) == FS_OK && task_initialize() &&
          large_loader_asset_end - large_loader_asset_start > 65536 &&
          test_file_put("LARGE.ELF", large_loader_asset_start, large_loader_asset_end - large_loader_asset_start) &&
          test_file_put("CANARY.TXT", "parent data", 11), "install large executable and parent canary");
    cpu_interrupt_enable(); pic_unmask(0);
    run('n'); concurrent();
    for (unsigned i = 0; i < 2; ++i) { run('p'); allocation_failures(); run('l'); run('f'); run('g'); run('s'); }
    serial_writestring("rum_large_loader_ok\n"); cpu_halt();
}
