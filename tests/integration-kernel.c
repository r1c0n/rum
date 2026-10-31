#include <rum/abi/syscall.h>
#include <rum/ata.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/fat16.h>
#include <rum/gdt.h>
#include <rum/heap.h>
#include <rum/keyboard.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/pic.h>
#include "test-files.h"
#include <rum/serial.h>
#include <rum/syscall.h>
#include <rum/task.h>
#include <rum/terminal.h>
#include <rum/timer.h>

#define REPORT 0xffffffd0u
#define PROGRESS 0xffffffd1u
void kernel_main(uint32_t magic, uint32_t information);
extern const char __kernel_start[], __kernel_end[];
extern const unsigned char integration_asset_start[], integration_asset_end[];
static struct { task_id id; uint32_t address, physical, value, reports; } probes[2];
static struct task_snapshot snapshot;
static struct rum_arguments arguments;
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void check(bool ok, const char *name)
{
    if (ok) return;
    serial_writestring("rum_integration_failed: "); serial_writestring(name); serial_writestring("\n"); cpu_halt();
}
static void number(uint32_t value)
{
    char digits[11]; unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) serial_putchar(digits[--n]);
}
void interrupt_dispatch(struct exception_frame *frame)
{
    if (frame->vector >= PIC_VECTOR_BASE && frame->vector < PIC_VECTOR_BASE + 16) { irq_dispatch(frame); return; }
    if (frame->vector != RUM_SYSCALL_VECTOR) exception_dispatch(frame);
    if (frame->eax == PROGRESS) { frame->eax = (probes[0].reports > 2) + (probes[1].reports > 2); return; }
    if (frame->eax != REPORT) { syscall_dispatch(frame); return; }
    unsigned index = frame->ebx - 1; uint32_t physical;
    struct paging_space *space = task_current_process_space(); struct task_information info;
    check(index < 2 && !(frame->ecx & (RUM_PAGE_SIZE - 1)) &&
          paging_user_accessible(space, frame->ecx, RUM_PAGE_SIZE, true) &&
          paging_translate(space, frame->ecx, &physical) &&
          task_query(task_current_id(), &info) && info.directory == paging_directory_address(paging_active_space()) &&
          info.stack_top == rum_tss.esp0, "CPU worker has its own trusted stack, CR3, TSS and checked page");
    if (probes[index].id) check(probes[index].id == info.id && probes[index].physical == physical, "stable private page identity");
    const volatile uint32_t *data = (const void *)(uintptr_t)physical;
    check(data[0] > probes[index].value && data[1] == ((frame->ebx * 0x112233u) ^ 1u) &&
          data[1023] == ((frame->ebx * 0x112233u) ^ 1023u), "independent CPU progress and private canaries");
    probes[index].id = info.id; probes[index].address = frame->ecx; probes[index].physical = physical;
    probes[index].value = data[0]; ++probes[index].reports;
    if (probes[0].id && probes[1].id)
        check(probes[0].address == probes[1].address && probes[0].physical != probes[1].physical,
              "identical virtual addresses map to separate physical pages");
    frame->eax = 0;
}
static void exercise(const char *mode)
{
    memset(probes, 0, sizeof probes);
    struct fs_statistics fs = fs_stats(); struct heap_statistics heap = heap_stats();
    uint32_t pages = pmm_stats().free_pages;
    arguments = (struct rum_arguments){ .argc = 3, .string_bytes = 12, .offsets = {0, 12}, .strings = "/stress.elf" };
    const char *text = mode;
    do { arguments.strings[arguments.string_bytes++] = *text; } while (*text++);
    arguments.offsets[2] = arguments.string_bytes;
    arguments.strings[arguments.string_bytes++] = '0'; ++arguments.string_bytes;
    struct task_process process;
    check(elf_load_process(integration_asset_start, integration_asset_end - integration_asset_start, &arguments, &process), "load supervisor");
    task_id root = task_create_foreground_process(&process); check(root, "publish root");
    struct task_information info; check(task_wait_process(root, &info), "wait for mixed workload");
    if (info.termination == TASK_TERMINATION_EXIT && info.exit_status != 0 && info.exit_status != 17) {
        serial_writestring("user status: "); number((uint32_t)info.exit_status); serial_writestring("\n");
    }
    bool normal = equal(mode, "normal"), cancel = equal(mode, "cancel"), fault = equal(mode, "parent-fault");
    check(info.termination == (cancel ? TASK_TERMINATION_CANCELLED : fault ? TASK_TERMINATION_FAULT : TASK_TERMINATION_EXIT) &&
          info.exit_status == (!normal && !fault && !cancel ? 17 : 0) && (!fault || info.fault.vector == 6) &&
          !task_handle_count(root) && task_foreground_end(root) && task_reap_process(root), "exit status, fault/cancel and root ownership");
    for (unsigned tries = 0; tries < RUM_TASK_CAPACITY * 4; ++tries) {
        (void)task_reap(); check(task_snapshot_read(&snapshot), "cleanup snapshot");
        if (!snapshot.processes) break;
        check(task_yield(), "drain adopted descendants");
    }
    check(task_snapshot_read(&snapshot) && snapshot.count == 2 && !snapshot.processes && !snapshot.event_waiters &&
          !snapshot.deadline_waiters && snapshot.created == snapshot.exited && snapshot.exited == snapshot.reaped &&
          !snapshot.console.sessions && !snapshot.console.members && !snapshot.console.session_references &&
          !snapshot.console.process_references && fs_stats().objects == fs.objects && fs_stats().references == fs.references &&
          heap_stats().allocations == heap.allocations && heap_stats().used_bytes == heap.used_bytes &&
          pmm_stats().free_pages + (heap_stats().mapped_bytes - heap.mapped_bytes) / RUM_PAGE_SIZE == pages,
          "processes, waits, sessions, file objects, heap, stacks and pages return to baseline");
    serial_writestring("rum_integration_round_ok: "); serial_writestring(mode); serial_writestring("\n");
}
void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize(); serial_initialize(); idt_initialize(); pic_initialize(); timer_initialize();
    check(keyboard_initialize() && magic == MULTIBOOT_BOOTLOADER_MAGIC && information &&
          pmm_initialize((const void *)(uintptr_t)information, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && fs_initialize() && task_initialize(), "fixture startup");
    cpu_interrupt_enable(); check(ata_initialize().error == BLOCK_OK && fat16_mount(ata_device()) == FS_OK, "real ATA/FAT16 mount");
    check(fs_replace(NULL, "/stress.elf", integration_asset_start, integration_asset_end - integration_asset_start) == FS_OK &&
          fs_replace(NULL, "/ready.FLG", "0", 1) == FS_OK, "disk fixtures");
    check(fs_mkdir(NULL, "/STRESS") == FS_OK, "stress directory");
    const char *names[] = {"/STRESS/IOA.BIN", "/STRESS/IOB.BIN", "/STRESS/HOLD.BIN", "/STRESS/FAULT.BIN"};
    for (unsigned i = 0; i < 4; ++i) check(fs_replace(NULL, names[i], NULL, 0) == FS_OK, "warm persistent fixtures");
    pic_unmask(0); pic_unmask(1);
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        exercise("normal"); exercise("exit"); exercise("parent-fault"); exercise("cancel");
    }
    check(snapshot.switches >= 100 && fat16_unmount() == FS_OK, "many context switches and all disk pins released");
    serial_writestring("rum_integration_ok: "); number(snapshot.created); serial_writestring(" processes, ");
    number(snapshot.switches); serial_writestring(" switches\n"); cpu_halt();
}
