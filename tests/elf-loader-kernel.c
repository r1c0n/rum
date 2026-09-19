#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rum/abi/elf.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/heap.h>
#include <rum/interrupts.h>
#include <rum/memory.h>
#include <rum/multiboot.h>
#include <rum/paging.h>
#include <rum/pic.h>
#include <rum/pmm.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/task.h>
#include <rum/terminal.h>

extern const char __kernel_start[], __kernel_end[];
extern const unsigned char elf_loader_asset_start[], elf_loader_asset_end[];
void kernel_main(uint32_t magic, uint32_t information);

static const struct rum_arguments arguments = {
    .argc = 2, .string_bytes = 17, .offsets = {0, 10},
    .strings = "hello.elf\0island",
};
static unsigned rejected;

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_elf_loader_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

static void reject(const void *image, size_t bytes, const struct rum_arguments *args)
{
    uint32_t before = pmm_stats().free_pages;
    struct task_process process;
    memset(&process, 0xA5, sizeof process);
    check(!elf_load_process(image, bytes, args, &process) && !process.space &&
          !memcmp(&process, &(struct task_process){0}, sizeof process) &&
          pmm_stats().free_pages == before, "invalid input changed ownership");
    ++rejected;
}

static void write_u16(unsigned char *bytes, size_t offset, uint16_t value)
{
    memcpy(bytes + offset, &value, sizeof value);
}

static void write_u32(unsigned char *bytes, size_t offset, uint32_t value)
{
    memcpy(bytes + offset, &value, sizeof value);
}

static void reject_u8(unsigned char *copy, size_t size, size_t offset, uint8_t value)
{
    memcpy(copy, elf_loader_asset_start, size);
    copy[offset] = value;
    reject(copy, size, &arguments);
}

static void reject_u16(unsigned char *copy, size_t size, size_t offset, uint16_t value)
{
    memcpy(copy, elf_loader_asset_start, size);
    write_u16(copy, offset, value);
    reject(copy, size, &arguments);
}

static void reject_u32(unsigned char *copy, size_t size, size_t offset, uint32_t value)
{
    memcpy(copy, elf_loader_asset_start, size);
    write_u32(copy, offset, value);
    reject(copy, size, &arguments);
}

static void zero_range(const struct paging_space *space, uint32_t address, uint32_t bytes)
{
    unsigned char buffer[128];
    while (bytes) {
        size_t chunk = bytes < sizeof buffer ? bytes : sizeof buffer;
        check(paging_copy_from_user(space, buffer, address, chunk), "read zeroed user range");
        for (size_t i = 0; i < chunk; ++i)
            check(!buffer[i], "BSS, padding, or unused stack byte is nonzero");
        address += (uint32_t)chunk;
        bytes -= (uint32_t)chunk;
    }
}

static void same_range(const struct paging_space *space, uint32_t address,
                       const unsigned char *expected, uint32_t bytes)
{
    unsigned char buffer[128];
    while (bytes) {
        size_t chunk = bytes < sizeof buffer ? bytes : sizeof buffer;
        check(paging_copy_from_user(space, buffer, address, chunk) &&
              !memcmp(buffer, expected, chunk), "loaded file bytes differ");
        address += (uint32_t)chunk;
        expected += chunk;
        bytes -= (uint32_t)chunk;
    }
}

static void unique_frame(uint32_t *frames, unsigned *count,
                         const struct paging_space *space, uint32_t address)
{
    uint32_t physical;
    check(*count < 64 && paging_translate(space, address, &physical), "translate loaded page");
    physical &= ~(RUM_PAGE_SIZE - 1u);
    for (unsigned i = 0; i < *count; ++i)
        check(frames[i] != physical, "ELF mappings share a physical frame");
    frames[(*count)++] = physical;
}

static void inspect_process(const struct task_process *process)
{
    size_t image_bytes = (size_t)(elf_loader_asset_end - elf_loader_asset_start);
    struct rum_elf_header header;
    memcpy(&header, elf_loader_asset_start, sizeof header);
    check(process->space && process->user_frame.core.eip == header.entry,
          "trusted process entry");

    uint32_t frames[64];
    unsigned frame_count = 0;
    for (uint32_t i = 0; i < header.phnum; ++i) {
        struct rum_elf_program program;
        memcpy(&program, elf_loader_asset_start + header.phoff + i * sizeof program,
               sizeof program);
        if (program.type != RUM_ELF_PT_LOAD || !program.memory_bytes) continue;
        check(program.offset <= image_bytes && program.file_bytes <= image_bytes - program.offset,
              "fixture program bounds");
        uint32_t start = program.address & ~(RUM_PAGE_SIZE - 1u);
        uint32_t end = (program.address + program.memory_bytes + RUM_PAGE_SIZE - 1u) &
                       ~(RUM_PAGE_SIZE - 1u);
        check(paging_user_accessible(process->space, start, end - start, false) &&
              paging_user_accessible(process->space, start, end - start, true) ==
                  ((program.flags & RUM_ELF_PF_W) != 0), "final segment permissions");
        for (uint32_t page = start; page < end; page += RUM_PAGE_SIZE)
            unique_frame(frames, &frame_count, process->space, page);
        zero_range(process->space, start, program.address - start);
        same_range(process->space, program.address,
                   elf_loader_asset_start + program.offset, program.file_bytes);
        zero_range(process->space, program.address + program.file_bytes,
                   end - program.address - program.file_bytes);
    }

    for (uint32_t page = RUM_USER_STACK_BASE; page < RUM_USER_STACK_TOP;
         page += RUM_PAGE_SIZE) {
        check(paging_user_accessible(process->space, page, RUM_PAGE_SIZE, true),
              "writable user stack page");
        unique_frame(frames, &frame_count, process->space, page);
    }
    check(!paging_user_accessible(process->space, RUM_USER_STACK_GUARD_BASE,
                                  RUM_PAGE_SIZE, false), "unmapped user stack guard");

    uint32_t strings = RUM_USER_STACK_TOP - arguments.string_bytes;
    uint32_t words_count = arguments.argc + RUM_ABI_STACK_FIXED_WORDS;
    uint32_t words_bytes = words_count * sizeof(uint32_t);
    uint32_t esp = (strings - words_bytes) & ~(RUM_STACK_ALIGNMENT - 1u);
    check(process->user_frame.esp == esp && !(esp % RUM_STACK_ALIGNMENT),
          "aligned initial stack pointer");
    uint32_t words[RUM_PROCESS_ARGUMENT_LIMIT + RUM_ABI_STACK_FIXED_WORDS];
    memset(words, 0xA5, sizeof words);
    check(paging_copy_from_user(process->space, words, esp, words_bytes) &&
          words[0] == arguments.argc && words[1] == strings &&
          words[2] == strings + arguments.offsets[1] &&
          !words[arguments.argc + 1] && !words[arguments.argc + 2],
          "argc, argv, and empty envp stack words");
    unsigned char copied[RUM_ABI_ARGUMENT_BYTES];
    check(paging_copy_from_user(process->space, copied, strings, arguments.string_bytes) &&
          !memcmp(copied, arguments.strings, arguments.string_bytes), "initial argument strings");
    zero_range(process->space, RUM_USER_STACK_BASE, esp - RUM_USER_STACK_BASE);
    zero_range(process->space, esp + words_bytes, strings - esp - words_bytes);

    struct paging_space_statistics statistics;
    check(paging_space_stats(process->space, &statistics) &&
          statistics.user_pages == frame_count, "loader page ownership ledger");
}

static void malformed_images(size_t image_bytes)
{
    struct rum_elf_header header;
    memcpy(&header, elf_loader_asset_start, sizeof header);
    struct rum_elf_program programs[RUM_ELF_PROGRAM_LIMIT];
    memcpy(programs, elf_loader_asset_start + header.phoff,
           header.phnum * sizeof programs[0]);
    unsigned loads[2] = {0};
    unsigned load_count = 0, stack = RUM_ELF_PROGRAM_LIMIT;
    for (unsigned i = 0; i < header.phnum; ++i) {
        if (programs[i].type == RUM_ELF_PT_LOAD && programs[i].memory_bytes && load_count < 2)
            loads[load_count++] = i;
        if (programs[i].type == RUM_ELF_PT_GNU_STACK) stack = i;
    }
    check(load_count == 2 && stack < header.phnum, "fixture program headers");
    size_t first = header.phoff + loads[0] * sizeof programs[0];
    size_t second = header.phoff + loads[1] * sizeof programs[0];
    size_t stack_offset = header.phoff + stack * sizeof programs[0];
    unsigned char *copy = kmalloc(image_bytes);
    check(copy != NULL, "malformed image buffer");

    reject_u8(copy, image_bytes, 4, 2);
    reject_u8(copy, image_bytes, 5, 2);
    reject_u8(copy, image_bytes, 7, 3);
    reject_u16(copy, image_bytes, 16, 3);
    reject_u16(copy, image_bytes, 18, 62);
    reject_u32(copy, image_bytes, 20, 0);
    reject_u32(copy, image_bytes, 24, 0);
    reject_u32(copy, image_bytes, 28, 0xFFFFFFF0u);
    reject_u16(copy, image_bytes, 40, 51);
    reject_u16(copy, image_bytes, 42, 31);
    reject_u16(copy, image_bytes, 44, 0);
    reject_u16(copy, image_bytes, 44, RUM_ELF_PROGRAM_LIMIT + 1);
    reject_u32(copy, image_bytes, first, 3);
    reject_u32(copy, image_bytes, first + 4, 0xFFFFFFF0u);
    reject_u32(copy, image_bytes, first + 8, 0);
    reject_u32(copy, image_bytes, first + 8, RUM_USER_PROGRAM_END);
    reject_u32(copy, image_bytes, first + 16, programs[loads[0]].memory_bytes + 1);
    reject_u32(copy, image_bytes, first + 20, RUM_PROCESS_USER_BYTES);
    reject_u32(copy, image_bytes, first + 20, UINT32_MAX);
    reject_u32(copy, image_bytes, first + 24, 7);
    reject_u32(copy, image_bytes, first + 28, 3);
    reject_u32(copy, image_bytes, first + 4, programs[loads[0]].offset + 1);
    reject_u32(copy, image_bytes, second + 8, programs[loads[0]].address);
    reject_u32(copy, image_bytes, stack_offset, RUM_ELF_PT_NULL);
    reject_u32(copy, image_bytes, stack_offset + 24, 7);
    reject(elf_loader_asset_start, 0, &arguments);
    reject(elf_loader_asset_start, sizeof(struct rum_elf_header) - 1, &arguments);
    reject(elf_loader_asset_start,
           header.phoff + header.phnum * sizeof(struct rum_elf_program) - 1, &arguments);
    reject(elf_loader_asset_start, RAMFS_FILE_LIMIT + 1, &arguments);
    check(kfree(copy), "release malformed image buffer");
}

static uint32_t consume_until(uint32_t remaining)
{
    uint32_t head = 0;
    while (pmm_stats().free_pages > remaining) {
        uint32_t page = pmm_allocate_page();
        check(page != 0, "consume physical pages");
        *(uint32_t *)(uintptr_t)page = head;
        head = page;
    }
    return head;
}

static void release_pages(uint32_t head)
{
    while (head) {
        uint32_t next = *(uint32_t *)(uintptr_t)head;
        check(pmm_free_page(head), "release consumed physical page");
        head = next;
    }
}

static void allocation_failures(size_t image_bytes)
{
    uint32_t baseline = pmm_stats().free_pages;
    struct task_process process;
    check(elf_load_process(elf_loader_asset_start, image_bytes, &arguments, &process),
          "measure loader allocation cost");
    uint32_t required = baseline - pmm_stats().free_pages;
    check(required && paging_space_destroy(process.space) &&
          pmm_stats().free_pages == baseline, "measure cleanup");
    for (uint32_t remaining = 0; remaining < required; ++remaining) {
        uint32_t held = consume_until(remaining);
        memset(&process, 0xA5, sizeof process);
        check(!elf_load_process(elf_loader_asset_start, image_bytes, &arguments, &process) &&
              !process.space && pmm_stats().free_pages == remaining,
              "loader allocation failure rollback");
        release_pages(held);
        check(pmm_stats().free_pages == baseline, "restore allocation failure ledger");
    }
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC && information, "Multiboot handoff");
    check(pmm_initialize((const void *)(uintptr_t)information,
                         (uintptr_t)__kernel_start, (uintptr_t)__kernel_end) &&
          paging_initialize() && heap_initialize() && ramfs_initialize() && task_initialize(),
          "loader fixture startup");
    size_t image_bytes = (size_t)(elf_loader_asset_end - elf_loader_asset_start);
    check(image_bytes && image_bytes <= RAMFS_FILE_LIMIT &&
          ramfs_put("hello.elf", elf_loader_asset_start, image_bytes), "install ELF RAM file");

    uint32_t baseline = pmm_stats().free_pages;
    struct rum_arguments bad = arguments;
    bad.argc = 0; reject(elf_loader_asset_start, image_bytes, &bad);
    bad = arguments; bad.argc = RUM_PROCESS_ARGUMENT_LIMIT + 1;
    reject(elf_loader_asset_start, image_bytes, &bad);
    bad = arguments; bad.string_bytes = 0; reject(elf_loader_asset_start, image_bytes, &bad);
    bad = arguments; bad.string_bytes = RUM_PROCESS_ARGUMENT_BYTES + 1;
    reject(elf_loader_asset_start, image_bytes, &bad);
    bad = arguments; bad.offsets[1] = 11; reject(elf_loader_asset_start, image_bytes, &bad);
    bad = arguments; bad.string_bytes = 9; reject(elf_loader_asset_start, image_bytes, &bad);
    bad = arguments; bad.string_bytes = 18; reject(elf_loader_asset_start, image_bytes, &bad);
    malformed_images(image_bytes);

    unsigned char *padded = kcalloc(1, RAMFS_FILE_LIMIT);
    check(padded != NULL, "64 KiB ELF boundary buffer");
    memcpy(padded, elf_loader_asset_start, image_bytes);
    struct task_process process;
    check(elf_load_process(padded, RAMFS_FILE_LIMIT, &arguments, &process),
          "accept exact RAM file size limit");
    inspect_process(&process);
    check(paging_space_destroy(process.space) && kfree(padded), "release boundary image");

    baseline = pmm_stats().free_pages;
    check(elf_load_process(elf_loader_asset_start, image_bytes, &arguments, &process),
          "load validated ELF bytes");
    inspect_process(&process);
    check(paging_space_destroy(process.space) && pmm_stats().free_pages == baseline,
          "direct load cleanup");
    allocation_failures(image_bytes);

    baseline = pmm_stats().free_pages;
    check(elf_load_ramfs("/hello.elf", &arguments, &process), "load ELF from RAM file");
    inspect_process(&process);
    cpu_interrupt_enable();
    task_id child = task_create_process(&process);
    check(child && task_yield(), "publish and run loaded process");
    struct task_information info;
    check(task_query(child, &info) && info.state == TASK_EXITED &&
          info.termination == TASK_TERMINATION_EXIT && info.exit_status == 0,
          "loaded program exit status");
    check(task_reap() == 1 && !task_query(child, &info) &&
          pmm_stats().free_pages == baseline, "loaded process cleanup");

    terminal_writestring("rum ELF loader tests passed.\n");
    serial_writestring("rum_elf_loader_rejected=");
    serial_putchar((char)('0' + rejected / 10));
    serial_putchar((char)('0' + rejected % 10));
    serial_writestring("\nrum_elf_loader_test_ok\n");
    cpu_halt();
}
