OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Follow Bare Bones: load the kernel at 2 MiB. */
    . = 2M;
    __kernel_start = .;
    .text ALIGN(4K) :
    {
        __text_start = .;
        KEEP(*(.multiboot))
        *(.text .text.*)
        __text_end = .;
    }
    .rodata ALIGN(4K) : { __rodata_start = .; *(.rodata .rodata.*) __rodata_end = .; }
    .data ALIGN(4K) : { *(.data .data.*) }
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }
    __kernel_end = ALIGN(4K);
    /DISCARD/ : { *(.comment) *(.eh_frame .eh_frame.*) }
}
