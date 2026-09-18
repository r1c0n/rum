/* Trusted build-checked ELF and fixture-owned page tables/mock syscalls only.
   This is not a production process loader or syscall dispatcher. */
#include <rum/abi/elf.h>
#include <rum/abi/error.h>
#include <rum/abi/layout.h>
#include <rum/abi/process.h>
#include <rum/abi/syscall.h>
#include <rum/cpu.h>
#include <rum/cpu_policy.h>
#include <rum/gdt.h>
#include <rum/memory.h>
#include <rum/pic.h>
#include <rum/serial.h>
#include <rum/terminal.h>
#include <rum/timer.h>

void kernel_main(uint32_t magic, uint32_t information);
void abi_test_dispatch(struct exception_frame *frame);
_Noreturn void abi_test_enter(const struct exception_user_frame *frame);
void abi_test_syscall(void);
extern const char abi_asset_start[], abi_asset_end[], __kernel_end[];
extern const char cpu_test_stack_top[];
extern const uint32_t abi_c_alignment;
uint32_t abi_directory[1024] __attribute__((aligned(4096)));
uint32_t abi_kernel_table[1024] __attribute__((aligned(4096)));
uint32_t abi_program_table[1024] __attribute__((aligned(4096)));
uint32_t abi_stack_table[1024] __attribute__((aligned(4096)));
unsigned char abi_program_pages[16 * 4096] __attribute__((aligned(4096)));
unsigned char abi_stack_pages[RUM_ABI_STACK_SIZE] __attribute__((aligned(4096)));
static unsigned output_bytes, irq_returns, read_calls;
static const char hello_output[] = "Hello from rum userspace!\n";

static void check(bool condition, const char *name)
{
    if (!condition) {
        serial_writestring("rum_abi_test_failed: ");
        serial_writestring(name);
        serial_writestring("\n");
        cpu_halt();
    }
}

void abi_test_dispatch(struct exception_frame *frame)
{
    check(exception_frame_from_user(frame) && frame->cs == USER_CODE_SELECTOR &&
          exception_frame_ss(frame) == USER_DATA_SELECTOR, "real ring-3 entry");
    check((uintptr_t)frame == (uintptr_t)cpu_test_stack_top - sizeof(struct exception_user_frame) &&
          abi_c_alignment == 12, "TSS stack and C alignment");
    check(exception_frame_esp(frame) >= RUM_ABI_STACK_BASE &&
          exception_frame_esp(frame) < RUM_ABI_STACK_TOP, "saved user stack");
    if (frame->vector == PIC_VECTOR_BASE) {
        ++irq_returns;
        irq_dispatch(frame);
        return;
    }
    check(frame->vector == RUM_SYSCALL_VECTOR && !frame->error, "int 0x80 entry");
    if (frame->eax == RUM_SYS_EXIT) {
        check((int32_t)frame->ebx == (RUM_USER_CASE == 2 ? 0 : -37), "main return reaches exit");
        check(output_bytes == (RUM_USER_CASE == 2 ? sizeof(hello_output) - 1 : 3u), "complete output with partial writes");
        if (RUM_USER_CASE != 2) check(read_calls == 1 && irq_returns >= 3, "read and real PIT/IRET");
        terminal_writestring("rum user ABI and startup tests passed.\n");
        serial_writestring("rum_abi_test_ok\n");
        cpu_halt();
    }
    if (frame->eax == RUM_SYS_GETPID) { frame->eax = 42; return; }
    if (frame->eax == 0xFFFFFFF0 && RUM_USER_CASE != 2) { frame->eax = timer_ticks(); return; }
    if (frame->eax == RUM_SYS_READ) {
        if (frame->ebx != RUM_STDIN) { frame->eax = (uint32_t)-RUM_EBADF; return; }
        check(frame->ecx >= RUM_ABI_PROGRAM_BASE && frame->ecx < RUM_ABI_PROGRAM_BASE + sizeof(abi_program_pages) - 3 &&
              frame->edx == 16 && RUM_USER_CASE != 2, "read wrapper arguments");
        memcpy((void *)(uintptr_t)frame->ecx, "rum", 3);
        ++read_calls;
        frame->eax = 3;
        return;
    }
    if (frame->eax == RUM_SYS_WRITE) {
        const char *expected = RUM_USER_CASE == 2 ? hello_output : "rum";
        unsigned total = RUM_USER_CASE == 2 ? sizeof(hello_output) - 1 : 3;
        check(frame->ebx == RUM_STDOUT && frame->ecx >= RUM_ABI_PROGRAM_BASE &&
              frame->ecx < RUM_ABI_PROGRAM_BASE + sizeof(abi_program_pages) &&
              frame->edx == total - output_bytes &&
              frame->edx <= RUM_ABI_PROGRAM_BASE + sizeof(abi_program_pages) - frame->ecx,
              "write wrapper arguments");
        check(!memcmp((const void *)(uintptr_t)frame->ecx, expected + output_bytes, frame->edx), "loaded output bytes");
        unsigned accepted = frame->edx < 3 ? frame->edx : 3;
        output_bytes += accepted;
        frame->eax = accepted;
        return;
    }
    frame->eax = (uint32_t)-RUM_ENOSYS;
}

static uint32_t prepare_stack(void)
{
    unsigned argc = RUM_USER_CASE == 1 ? RUM_ABI_ARGUMENT_LIMIT : 2;
    unsigned bytes = RUM_USER_CASE == 1 ? RUM_ABI_ARGUMENT_BYTES : 14;
    uint32_t strings = RUM_ABI_STACK_TOP - bytes;
    uint32_t esp = (strings - (argc + RUM_ABI_STACK_FIXED_WORDS) * 4) & ~15u;
    uint32_t *words = (void *)(uintptr_t)esp;
    words[0] = argc;
    uint32_t cursor = strings;
    for (unsigned i = 0; i < argc; ++i) {
        unsigned length = i == 0 ? 10 : argc == 2 ? 4 :
                          i == argc - 1 ? RUM_ABI_ARGUMENT_BYTES - 10 - 30 * 128 : 128;
        words[i + 1] = cursor;
        char *text = (void *)(uintptr_t)cursor;
        if (i == 0) memcpy(text, "abi-probe", length);
        else if (argc == 2) memcpy(text, "rum", length);
        else { memset(text, 'A' + i % 26, length - 1); text[length - 1] = 0; }
        cursor += length;
    }
    words[argc + 1] = words[argc + 2] = 0;
    check(cursor == RUM_ABI_STACK_TOP, "bounded stack construction");
    return esp;
}

void kernel_main(uint32_t magic, uint32_t information)
{
    terminal_initialize();
    serial_initialize();
    idt_initialize();
    pic_initialize();
    check(magic == 0x2BADB002 && information && (uintptr_t)__kernel_end < 0x400000,
          "fixture identity window and Multiboot");
    const struct rum_elf_header *header = (const void *)abi_asset_start;
    const struct rum_elf_program *programs = (const void *)(abi_asset_start + header->phoff);
    check(header->phnum <= RUM_ELF_PROGRAM_LIMIT, "trusted ELF headers");
    memset(abi_program_pages, 0, sizeof(abi_program_pages));
    memset(abi_stack_pages, 0, sizeof(abi_stack_pages));
    for (unsigned i = 0; i < header->phnum; ++i) {
        const struct rum_elf_program *p = &programs[i];
        if (p->type != RUM_ELF_PT_LOAD || !p->memory_bytes) continue;
        check(p->address >= RUM_ABI_PROGRAM_BASE && p->address - RUM_ABI_PROGRAM_BASE < sizeof(abi_program_pages) &&
              p->memory_bytes <= sizeof(abi_program_pages) - (p->address - RUM_ABI_PROGRAM_BASE) &&
              p->file_bytes <= p->memory_bytes && p->offset <= (uint32_t)(abi_asset_end - abi_asset_start) &&
              p->file_bytes <= (uint32_t)(abi_asset_end - abi_asset_start) - p->offset, "fixture segment bounds");
        unsigned start = (p->address - RUM_ABI_PROGRAM_BASE) / 4096;
        unsigned end = (p->address - RUM_ABI_PROGRAM_BASE + p->memory_bytes + 4095) / 4096;
        for (unsigned page = start; page < end; ++page) {
            check(!abi_program_table[page], "fixture segments have separate pages");
            abi_program_table[page] = (uintptr_t)abi_program_pages + page * 4096 +
                                     ((p->flags & RUM_ELF_PF_W) ? 7 : 5);
        }
        memcpy(abi_program_pages + (p->address - RUM_ABI_PROGRAM_BASE), abi_asset_start + p->offset, p->file_bytes);
    }
    for (unsigned page = 1; page < 1024; ++page) abi_kernel_table[page] = page * 4096 | 3;
    for (unsigned page = 0; page < 16; ++page) abi_stack_table[1008 + page] =
        ((uintptr_t)abi_stack_pages + page * 4096) | 7;
    abi_directory[0] = (uintptr_t)abi_kernel_table | 3;
    abi_directory[RUM_ABI_PROGRAM_BASE >> 22] = (uintptr_t)abi_program_table | 7;
    abi_directory[(RUM_ABI_STACK_TOP - 1) >> 22] = (uintptr_t)abi_stack_table | 7;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uintptr_t)abi_directory) : "memory");
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0 | 0x80010000) : "memory");
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idtr;
    __asm__ volatile ("sidt %0" : "=m"(idtr));
    struct gate { uint16_t low, selector; uint8_t zero, attributes; uint16_t high; } __attribute__((packed));
    struct gate *gates = (void *)(uintptr_t)idtr.base;
    uintptr_t target = (uintptr_t)abi_test_syscall;
    gates[RUM_SYSCALL_VECTOR] = (struct gate){target & 0xFFFF, KERNEL_CODE_SELECTOR, 0, 0xEE, target >> 16};
    check(gdt_set_kernel_stack((uintptr_t)cpu_test_stack_top), "dedicated TSS entry stack");
    timer_initialize();
    pic_unmask(0);
    struct exception_user_frame user;
    cpu_user_frame_initialize(&user, header->entry, prepare_stack());
    abi_test_enter(&user);
}
