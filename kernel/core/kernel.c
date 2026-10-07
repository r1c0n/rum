#include <stdint.h>
#include <rum/ata.h>
#include <rum/cpu.h>
#include <rum/fat16.h>
#include <rum/interrupts.h>
#include <rum/heap.h>
#include <rum/keyboard.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/pmm.h>
#include <rum/process.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/terminal.h>
#include <rum/timer.h>
#include <rum/task.h>

#if defined(__linux__) || defined(_WIN32)
#error "rum needs a bare-metal i686-elf compiler, not a Linux or Windows compiler."
#endif
#if !defined(__i386__)
#error "rum's first kernel targets 32-bit x86."
#endif

extern const char __kernel_start[], __kernel_end[];

static void serial_number(uint32_t number)
{
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    while (count) serial_putchar(digits[--count]);
}

static void print(const char *text)
{
    terminal_writestring(text);
    serial_writestring(text);
}

static void update_uptime(uint32_t seconds)
{
    char text[32] = "uptime: ";
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + seconds % 10);
        seconds /= 10;
    } while (seconds);
    unsigned length = 8;
    while (count)
        text[length++] = digits[--count];
    text[length++] = 's';
    text[length] = '\0';
    terminal_status(text);
}

static bool recovery;
static bool disk_readonly;
static char shell_path[FS_PATH_CAPACITY] = "/shell.elf";
static volatile bool uptime_stop;

/* PMM has already bounded/reserved the Multiboot command string. Parse it
   before paging replaces the bootloader's address-space assumptions. */
static void boot_options(const struct multiboot_info *info)
{
    if (!(info->flags & (1u << 2)) || !info->cmdline) return;
    const char *text = (const void *)(uintptr_t)info->cmdline;
    unsigned cursor = 0;
    while (cursor < PAGE_SIZE && text[cursor]) {
        while (cursor < PAGE_SIZE && text[cursor] == ' ') ++cursor;
        unsigned start = cursor;
        while (cursor < PAGE_SIZE && text[cursor] && text[cursor] != ' ') ++cursor;
        unsigned bytes = cursor - start;
        if (bytes == 12 && !memcmp(text + start, "rum.recovery", 12)) recovery = true;
        if (bytes == 17 && !memcmp(text + start, "rum.disk-readonly", 17)) disk_readonly = true;
        if (bytes >= 10 && !memcmp(text + start, "rum.shell=", 10)) {
            if (bytes == 10 || bytes - 10 >= sizeof shell_path) recovery = true;
            else { memcpy(shell_path, text + start + 10, bytes - 10); shell_path[bytes - 10] = 0; }
        }
    }
}

static void uptime_worker(void *unused)
{
    (void)unused;
    uint32_t displayed = UINT32_MAX;
    while (!uptime_stop) {
        uint32_t observed = task_event_sequence(task_work_event());
        uint32_t seconds = timer_ticks() / TIMER_HZ;
        if (seconds != displayed) { displayed = seconds; update_uptime(seconds); }
        if (!uptime_stop) (void)task_wait(task_work_event(), observed);
    }
}

static void userspace_session(void)
{
    /* Supervisor owns the root shell; children are owned by the shell's RUN
       continuation. The helper never reads keyboard input. */
    task_id uptime = task_create(uptime_worker, NULL, NULL);
    static struct rum_arguments arguments;
    arguments.argc = 1;
    while (shell_path[arguments.string_bytes]) ++arguments.string_bytes;
    ++arguments.string_bytes;
    memcpy(arguments.strings, shell_path, arguments.string_bytes);
    unsigned failures = 0;
    while (failures < 3) {
        task_id child;
        rum_result_t error = process_start_foreground(shell_path, &arguments, &child);
        if (error) { print("Cannot load the userspace shell.\n"); break; }
        if (!task_mark_foreground_shell(child)) cpu_halt();
        uint32_t started = timer_ticks();
        struct task_information result;
        if (!task_wait_process(child, &result) || !task_foreground_end(child) || !task_reap_process(child)) cpu_halt();
        if (result.termination == TASK_TERMINATION_EXIT && result.exit_status == RUM_SHELL_RECOVERY_STATUS) break;
        /* A session lasting 30 seconds starts a new restart window. */
        if ((uint32_t)(timer_ticks() - started) >= TIMER_HZ * 30u) failures = 0;
        if (++failures == 3) { print("Userspace shell stopped repeatedly.\n"); break; }
        print("Restarting the userspace shell.\n");
    }
    if (uptime) {
        uptime_stop = true;
        task_event_signal(task_work_event());
        struct task_information result;
        while (task_query(uptime, &result) && result.state != TASK_EXITED) (void)task_yield();
        (void)task_reap();
    }
    /* Stale keys from an exiting shell do not become recovery commands. */
    char unused;
    while (keyboard_read(&unused)) {}
}

static void recovery_initialize(void)
{
    print("Kernel recovery shell.\n");
    shell_set_launcher(process_launch_foreground);
    shell_initialize();
}

static _Noreturn void recovery_session(void)
{
    uint32_t displayed = UINT32_MAX;
    for (;;) {
        uint32_t flags = cpu_interrupt_save();
        uint32_t observed = task_event_sequence(task_work_event());
        uint32_t ticks = timer_ticks();
        uint32_t seconds = ticks / TIMER_HZ;
        char character;
        bool available = keyboard_read(&character);
        if (seconds != displayed || available || shell_tick_due(ticks)) {
            cpu_interrupt_restore(flags);
            if (seconds != displayed) { displayed = seconds; update_uptime(seconds); }
            if (available) shell_receive(character);
            shell_tick(timer_ticks());
        } else {
            cpu_interrupt_restore(flags);
            (void)task_wait(task_work_event(), observed);
        }
    }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address);

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    timer_initialize();
    bool keyboard_ready = keyboard_initialize();
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC || multiboot_info_address == 0) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: invalid Multiboot handoff. Halting.\n");
        return;
    }
    const struct multiboot_info *info = (const void *)(uintptr_t)multiboot_info_address;
    if (!pmm_initialize(info, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end)) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: missing or invalid Multiboot memory map. Halting.\n");
        return;
    }
    boot_options(info);
    if (!paging_initialize()) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: cannot allocate page tables. Halting.\n");
        return;
    }
    if (!heap_initialize() || !ramfs_initialize() || !embedded_files_install()) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: cannot initialize heap or RAM files. Halting.\n");
        return;
    }
    if (!task_initialize()) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("rum: cannot initialize kernel contexts. Halting.\n");
        return;
    }
    struct block_result disk = ata_initialize();
    serial_writestring("rum_disk: ");
    serial_writestring(block_error_name(disk.error));
    serial_writestring(" status="); serial_number(disk.status);
    serial_writestring(" error="); serial_number(disk.device_error);
    serial_writestring(" sectors="); serial_number((uint32_t)ata_device()->sector_count);
    serial_writestring("\n");
    enum fs_error filesystem = disk_readonly ? fat16_mount_read_only(ata_device()) : fat16_mount(ata_device());
    serial_writestring("rum_fat16: "); serial_writestring(fs_error_name(filesystem)); serial_writestring("\n");
    struct pmm_statistics memory = pmm_stats();
    serial_writestring("rum_memory_ok info="); serial_number(multiboot_info_address);
    serial_writestring(" usable="); serial_number(memory.usable_pages);
    serial_writestring(" managed="); serial_number(memory.managed_pages);
    serial_writestring(" free="); serial_number(memory.free_pages);
    serial_writestring(" limit="); serial_number(memory.limit);
    serial_writestring(" directory="); serial_number(paging_directory_address(paging_kernel_space()));
    serial_writestring("\n");
    struct heap_statistics heap = heap_stats();
    struct ramfs_statistics files = ramfs_stats();
    serial_writestring("rum_storage_ok mapped="); serial_number(heap.mapped_bytes);
    serial_writestring(" used="); serial_number(heap.used_bytes);
    serial_writestring(" allocations="); serial_number(heap.allocations);
    serial_writestring(" files="); serial_number(files.files);
    serial_writestring(" bytes="); serial_number(files.bytes);
    serial_writestring("\n");

    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    print("  rum\n");
    terminal_set_color(VGA_WHITE, VGA_BLACK);
    print("  An island of our own.\n");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("  rum OS v0.3.0 | 32-bit x86\n");
    print("  Hello, kernel world!\n");
    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print("  [ok] Multiboot handoff\n");
    print("  [ok] C kernel and stack\n");
    print("  [ok] VGA text console\n");
    print("  [ok] COM1 serial logging\n");
    print("  [ok] Kernel GDT and segments\n");
    print("  [ok] IDT and CPU exception handlers\n");
    print("  [ok] Multiboot memory map\n");
    print("  [ok] Physical page allocator\n");
    print("  [ok] Paging (4 KiB pages, null guard)\n");
    print("  [ok] Kernel heap (16-byte alignment)\n");
    print("  [ok] RAM filesystem and embedded files\n");
    if (filesystem == FS_OK) print(fat16_info().writable ?
        "  [ok] FAT16 /disk (read/write)\n" : "  [ok] FAT16 /disk (read only)\n");
    print("  [ok] PIC remapped (IRQ0/IRQ1 only)\n");
    print("  [ok] PIT timer at 100 Hz\n");
    if (keyboard_ready) {
        print("  [ok] PS/2 keyboard (US layout)\n\n");
    } else {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        print("  [failed] PS/2 keyboard initialization\n\n");
    }
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("  Type 'help' for commands; Backspace edits.\n");
    print("  Close QEMU to return to your host.\n");
    update_uptime(0);
    pic_unmask(0);
    if (keyboard_ready)
        pic_unmask(1);
    if (recovery) recovery_initialize();
    serial_writestring("rum_boot_ok\n");
    cpu_interrupt_enable();
    if (!recovery) { userspace_session(); recovery_initialize(); }
    recovery_session();
}
