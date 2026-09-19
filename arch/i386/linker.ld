#include <rum/memory_layout.h>

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Follow Bare Bones: load the kernel at 2 MiB. */
    . = RUM_KERNEL_LOAD_BASE;
    __kernel_start = .;
    .text ALIGN(RUM_PAGE_SIZE) :
    {
        __text_start = .;
        KEEP(*(.multiboot))
        *(.text .text.*)
        __text_end = .;
    }
    .rodata ALIGN(RUM_PAGE_SIZE) : { __rodata_start = .; *(.rodata .rodata.*) __rodata_end = .; }
    .data ALIGN(RUM_PAGE_SIZE) : { *(.data .data.*) }
    .bss ALIGN(RUM_PAGE_SIZE) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }
    __kernel_end = ALIGN(RUM_PAGE_SIZE);
    ASSERT(__kernel_end <= RUM_IDENTITY_END, "kernel exceeds identity window")
    ASSERT(__boot_stack_top - __boot_stack_bottom == RUM_BOOT_STACK_SIZE,
           "boot stack size differs from memory layout")
    ASSERT(__boot_stack_bottom >= __kernel_start && __boot_stack_top <= __kernel_end,
           "boot stack is outside reserved kernel memory")
    /DISCARD/ : { *(.comment) *(.eh_frame .eh_frame.*) }
}
