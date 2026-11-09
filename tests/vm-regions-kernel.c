#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/handles.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "test-files.h"

#define REPORT 0xffffffe0u
#define PEERS  0xffffffe1u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char vm_regions_asset_start[], vm_regions_asset_end[];
static struct { uint32_t address, physical; bool ready; } reports[2];
static struct rum_arguments arguments;
static struct task_snapshot snapshot;

static void check(bool ok, const char *name)
{
    if (ok) return;
    serial_writestring("rum_vm_regions_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) { irq_dispatch(frame); return; }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax == PEERS) { frame->eax = reports[0].ready + reports[1].ready; return; }
    if (frame->eax != REPORT) { syscall_dispatch(frame); return; }
    unsigned index = frame->edx - 1;
    struct paging_space *space = task_current_process_space();
    struct paging_space_statistics memory; struct task_information info;
    uint32_t physical, last;
    check(index < 2 && paging_space_stats(space, &memory) && memory.dynamic_regions == 1 &&
          task_query(task_current_id(), &info) && info.resources.user_pages == memory.user_pages &&
          info.resources.user_table_pages == memory.private_table_pages &&
          paging_user_accessible(space, frame->ebx, frame->ecx, true) &&
          paging_translate(space, frame->ebx, &physical) &&
          paging_translate(space, frame->ebx + frame->ecx - 4, &last) &&
          *(uint32_t *)(uintptr_t)physical == frame->edx &&
          *(uint32_t *)(uintptr_t)last == (frame->edx ^ 0x11223344),
          "live process registry, accounting and data canaries");
    reports[index].address = frame->ebx; reports[index].physical = physical; reports[index].ready = true;
    if (reports[0].ready && reports[1].ready)
        check(reports[0].address == reports[1].address && reports[0].physical != reports[1].physical,
              "equal user addresses own separate physical pages");
    frame->eax = 0;
}
static void make_arguments(char mode, unsigned index)
{
    arguments = (struct rum_arguments){ .argc = 3, .string_bytes = 11,
        .offsets = {0, 7, 9}, .strings = "vm.elf\0x\0x" };
    arguments.strings[7] = mode; arguments.strings[9] = (char)('0' + index);
}
static uint32_t consume(uint32_t keep)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages > keep) {
        uint32_t page = pmm_allocate_page(); check(page != 0, "exhaust physical pages");
        *(uint32_t *)(uintptr_t)page = head; head = page;
    }
    return head;
}
static void release(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "return held pages"); head = next;
    }
}
static void exercise(char mode)
{
    serial_writestring("rum_vm_case "); serial_putchar(mode); serial_writestring("\n");
    struct paging_statistics baseline = paging_stats();
    struct heap_statistics heap = heap_stats();
    struct process_handle_statistics handles = process_handles_stats();
    uint32_t pages = pmm_stats().free_pages;
    memset(reports, 0, sizeof reports);
    unsigned count = mode == 'n' ? 2 : 1;
    task_id children[2] = {0};
    for (unsigned i = 0; i < count; ++i) {
        make_arguments(mode, i);
        struct task_process process;
        check(elf_load_process(vm_regions_asset_start, vm_regions_asset_end - vm_regions_asset_start,
                               &arguments, &process), "load memory-test process");
        children[i] = i ? task_create_process(&process) : task_create_foreground_process(&process);
        check(children[i] != 0, "publish memory-test process");
    }
    uint32_t held = mode == 'o' ? consume(0) : 0;
    if (mode == 's') {
        uint32_t start = timer_ticks();
        while (!reports[0].ready) check(timer_ticks() - start < 200 && task_yield(), "run CPU-bound memory owner");
        check(task_cancel_foreground(), "Ctrl+C cancellation with live regions");
    }
    for (unsigned i = 0; i < count; ++i) {
        struct task_information info;
        check(task_wait_process(children[i], &info), "wait for memory-test process");
        if (info.exit_status) { serial_writestring("user failure code: ");
            serial_putchar((char)('0' + (uint32_t)info.exit_status / 10));
            serial_putchar((char)('0' + (uint32_t)info.exit_status % 10)); serial_writestring("\n"); }
        check(mode == 'f' ? info.termination == TASK_TERMINATION_FAULT && info.fault.vector == 6 :
              mode == 's' ? info.termination == TASK_TERMINATION_CANCELLED :
              info.termination == TASK_TERMINATION_EXIT && info.exit_status == 0, "expected termination");
        check((i || task_foreground_end(children[i])) && task_reap_process(children[i]), "reap memory owner");
    }
    if (held) release(held);
    struct paging_statistics after = paging_stats();
    struct process_handle_statistics closed = process_handles_stats();
    check(after.spaces == baseline.spaces && after.user_pages == baseline.user_pages &&
          after.private_table_pages == baseline.private_table_pages && after.regions == baseline.regions &&
          after.dynamic_regions == baseline.dynamic_regions && after.metadata_bytes == baseline.metadata_bytes &&
          closed.descriptions == handles.descriptions && closed.descriptors == handles.descriptors &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages &&
          task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.processes,
          "exit, fault, cancellation and OOM restore ownership baseline");
}
static void construction_failures(void)
{
    for (unsigned keep = 0; keep < 4; ++keep) {
        struct paging_statistics baseline = paging_stats();
        struct heap_statistics heap = heap_stats();
        struct process_handle_statistics handles = process_handles_stats();
        uint32_t free = pmm_stats().free_pages;
        make_arguments('i', 0);
        struct task_process process;
        check(elf_load_process(vm_regions_asset_start, vm_regions_asset_end - vm_regions_asset_start,
                               &arguments, &process), "prepare failed construction owner");
        struct rum_vm_allocate_request region = { .version = RUM_VM_ABI_VERSION, .size = sizeof region,
            .bytes = 2 * RUM_PAGE_SIZE, .protection = RUM_VM_READ | RUM_VM_WRITE };
        check(paging_region_allocate(process.space, &region) == 0, "prepare owned region before construction");
        uint32_t held = consume(keep);
        check(!task_create_process(&process) && paging_user_accessible(process.space, region.address, region.bytes, true),
              "failed stack construction leaves address space with producer");
        release(held);
        check(paging_space_destroy(process.space), "producer destroys unpublished regions");
        struct paging_statistics after = paging_stats();
        check(after.regions == baseline.regions && after.metadata_bytes == baseline.metadata_bytes &&
              after.user_pages == baseline.user_pages && after.private_table_pages == baseline.private_table_pages &&
              heap_stats().used_bytes == heap.used_bytes && heap_stats().allocations == heap.allocations &&
              process_handles_stats().descriptors == handles.descriptors &&
              process_handles_stats().descriptions == handles.descriptions &&
              pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == free,
              "partial process construction leaks no region resources");
    }
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && task_initialize(), "fixture startup");
    cpu_interrupt_enable(); pic_unmask(0);
    construction_failures();
    for (unsigned i = 0; i < 3; ++i) {
        exercise('i'); exercise('n'); exercise('l'); exercise('f'); exercise('s'); exercise('o');
    }
    serial_writestring("rum_vm_regions_ok\n"); cpu_halt();
}
