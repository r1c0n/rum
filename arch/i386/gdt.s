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

/* A task gate reaches this entry without touching the failed stack. The TSS
   supplies flat kernel segments, the kernel CR3 and the emergency stack. */
.global double_fault_entry
.type double_fault_entry, @function
double_fault_entry:
    cli
    cld
    and $-16, %esp
    call double_fault_dispatch
    jmp cpu_halt
.size double_fault_entry, . - double_fault_entry

.section .note.GNU-stack, "", @progbits
