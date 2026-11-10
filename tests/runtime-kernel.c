#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/handles.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/pipe.h>
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include "test-files.h"

#define REPORT 0xffffffd0u
#define PEERS 0xffffffd1u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char runtime_asset_start[], runtime_asset_end[];
static struct { bool ready; uint32_t address, physical; } reports[2];
static void check(bool ok, const char *name)
{
    if (ok) return;
    serial_writestring("rum_runtime_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) { irq_dispatch(frame); return; }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax == PEERS) { frame->eax = reports[0].ready + reports[1].ready; return; }
    if (frame->eax != REPORT) { syscall_dispatch(frame); return; }
    struct paging_space *space = task_current_process_space();
    struct paging_space_statistics memory;
    check(paging_space_stats(space, &memory), "query live runtime regions");
    if (!frame->ebx) { frame->eax = memory.dynamic_regions; return; }
    unsigned i = frame->edx - 1; uint32_t physical, last;
    check(i < 2 && memory.dynamic_regions && paging_user_accessible(space, frame->ebx, frame->ecx, true) &&
          paging_translate(space, frame->ebx, &physical) &&
          paging_translate(space, frame->ebx + frame->ecx - 1, &last) &&
          *(unsigned char *)(uintptr_t)physical == i + 1 && *(unsigned char *)(uintptr_t)last == i + 1,
          "heap ownership and canaries");
    reports[i].address = frame->ebx; reports[i].physical = physical; reports[i].ready = true;
    if (reports[0].ready && reports[1].ready)
        check(reports[0].address == reports[1].address && reports[0].physical != reports[1].physical,
              "equal heap addresses map private frames");
    frame->eax = 0;
}
static uint32_t consume(void)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages) {
        uint32_t page = pmm_allocate_page(); check(page != 0, "consume available physical RAM");
        *(uint32_t *)(uintptr_t)page = head; head = page;
    }
    return head;
}
static void release(uint32_t head)
{
    while (head) { uint32_t next = *(uint32_t *)(uintptr_t)head; check(pmm_free_page(head), "release held frame"); head = next; }
}
static void exercise(char mode)
{
    serial_writestring("rum_runtime_case "); serial_putchar(mode); serial_writestring("\n");
    struct paging_statistics before = paging_stats();
    struct heap_statistics heap = heap_stats();
    struct process_handle_statistics handles = process_handles_stats();
    uint32_t pages = pmm_stats().free_pages;
    memset(reports, 0, sizeof reports);
    task_id children[2] = {0}; unsigned count = mode == 'n' ? 2 : 1;
    for (unsigned i = 0; i < count; ++i) {
        struct rum_arguments arguments = { .argc = 3, .string_bytes = 10,
            .offsets = {0, 6, 8}, .strings = "r.elf\0x\0x" };
        arguments.strings[6] = mode; arguments.strings[8] = (char)('0' + i);
        struct task_process process;
        check(elf_load_process(runtime_asset_start, runtime_asset_end - runtime_asset_start, &arguments, &process), "load runtime test");
        children[i] = i ? task_create_process(&process) : task_create_foreground_process(&process);
        check(children[i] != 0, "publish runtime process");
    }
    uint32_t held = mode == 'o' ? consume() : 0;
    if (mode == 's') {
        uint32_t start = timer_ticks();
        while (!reports[0].ready) check(timer_ticks() - start < 200 && task_yield(), "reach live heap before cancellation");
        check(task_cancel_foreground(), "cancel CPU-bound owner");
    }
    for (unsigned i = 0; i < count; ++i) {
        struct task_information info;
        check(task_wait_process(children[i], &info), "wait for runtime process");
        if (info.exit_status) { serial_writestring("user failure code: ");
            serial_putchar((char)('0' + (uint32_t)info.exit_status / 10)); serial_putchar((char)('0' + (uint32_t)info.exit_status % 10)); serial_writestring("\n"); }
        check(mode == 'f' ? info.termination == TASK_TERMINATION_FAULT && info.fault.vector == 6 :
              mode == 's' ? info.termination == TASK_TERMINATION_CANCELLED :
              info.termination == TASK_TERMINATION_EXIT && info.exit_status == 0, "expected runtime termination");
        check((i || task_foreground_end(children[i])) && task_reap_process(children[i]), "reap runtime owner");
    }
    if (held) release(held);
    struct paging_statistics after = paging_stats();
    struct process_handle_statistics closed = process_handles_stats();
    check(after.spaces == before.spaces && after.user_pages == before.user_pages &&
          after.private_table_pages == before.private_table_pages && after.regions == before.regions &&
          after.dynamic_regions == before.dynamic_regions && after.metadata_bytes == before.metadata_bytes &&
          after.buffer_users == before.buffer_users && closed.descriptions == handles.descriptions &&
          closed.descriptors == handles.descriptors && heap_stats().allocations == heap.allocations &&
          heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "exit, fault, cancellation and OOM restore page, region, handle and metadata counts");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && test_files_initialize() && test_files_mount() && task_initialize() &&
          test_file_put("LEAK.TXT", "runtime", 7), "runtime fixture startup");
    cpu_interrupt_enable(); pic_unmask(0);
    for (unsigned i = 0; i < 3; ++i) {
        exercise('i'); exercise('n'); exercise('e'); exercise('o'); exercise('l'); exercise('f'); exercise('s');
    }
    serial_writestring("rum_runtime_ok\n"); cpu_halt();
}
