/* A flat ring-0 address space: null, executable code, read/write data.
   Accessed bits are preset so the CPU need not write into this read-only table. */
.section .rodata, "a"
.balign 8
.global rum_gdt
rum_gdt:
    .quad 0
    .quad 0x00CF9B000000FFFF
    .quad 0x00CF93000000FFFF
gdt_end:
rum_gdt_descriptor:
    .word gdt_end - rum_gdt - 1
    .long rum_gdt

.section .text, "ax"
.code32
.global gdt_initialize
.type gdt_initialize, @function
gdt_initialize:
    lgdt rum_gdt_descriptor
    ljmp $0x08, $1f
1:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret
.size gdt_initialize, . - gdt_initialize

.global cpu_halt
.type cpu_halt, @function
cpu_halt:
    cli
1:
    hlt
    jmp 1b
.size cpu_halt, . - cpu_halt

.section .note.GNU-stack, "", @progbits
