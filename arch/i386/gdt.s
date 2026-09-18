#include <rum/cpu_layout.h>

.section .text, "ax"
.code32
.global gdt_load
.type gdt_load, @function
gdt_load:
    mov 4(%esp), %eax
    lgdt (%eax)
    ljmp $KERNEL_CODE_SELECTOR, $1f
1:
    mov $KERNEL_DATA_SELECTOR, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret
.size gdt_load, . - gdt_load

.global cpu_halt
.type cpu_halt, @function
cpu_halt:
    cli
1:
    hlt
    jmp 1b
.size cpu_halt, . - cpu_halt

.section .note.GNU-stack, "", @progbits
