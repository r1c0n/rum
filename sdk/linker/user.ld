#include <rum/abi/layout.h>

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = RUM_ABI_PROGRAM_BASE;
    __user_start = .;
    .text ALIGN(RUM_ABI_PAGE_SIZE) : { KEEP(*(.text.start)) *(.text .text.*) } :text
    .rodata ALIGN(RUM_ABI_PAGE_SIZE) : { *(.rodata .rodata.*) } :rodata
    .data ALIGN(RUM_ABI_PAGE_SIZE) : { *(.data .data.*) } :data
    .bss ALIGN(16) (NOLOAD) : { *(COMMON) *(.bss .bss.*) } :data
    __user_end = ALIGN(RUM_ABI_PAGE_SIZE);
    ASSERT(__user_end <= RUM_ABI_PROGRAM_END, "user image exceeds program window")
    /* Initial 16 MiB process policy, reserving the 64 KiB user stack. */
    ASSERT(__user_end - __user_start <= RUM_ABI_MEMORY_BYTES - RUM_ABI_STACK_SIZE,
           "user image exceeds initial process budget")
    .unsupported : {
        *(.interp .dynamic .dynsym .dynstr .hash .gnu.hash)
        *(.rel.* .rela.* .plt .plt.* .got .got.* .tdata .tdata.* .tbss .tbss.*)
        *(.init_array .init_array.* .fini_array .fini_array.* .ctors .dtors)
    } :data
    ASSERT(SIZEOF(.unsupported) == 0, "unsupported dynamic/TLS/constructor sections")
    /DISCARD/ : { *(.comment .note.* .eh_frame .eh_frame.*) }
}
