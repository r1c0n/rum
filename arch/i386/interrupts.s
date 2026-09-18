#include <rum/cpu_layout.h>
.section .text, "ax"
.code32

/* Normalize the stack to vector, error, EIP, CS, EFLAGS. Hardware supplies
   error codes for the ERR entries; the others need a synthetic zero. */
.macro NOERR vector
.type exception_\vector, @function
exception_\vector:
    pushl $0
    pushl $\vector
    jmp interrupt_common
.size exception_\vector, . - exception_\vector
.endm

.macro ERR vector
.type exception_\vector, @function
exception_\vector:
    pushl $\vector
    jmp interrupt_common
.size exception_\vector, . - exception_\vector
.endm

NOERR 0
NOERR 1
NOERR 2
NOERR 3
NOERR 4
NOERR 5
NOERR 6
NOERR 7
ERR 8
NOERR 9
ERR 10
ERR 11
ERR 12
ERR 13
ERR 14
NOERR 15
NOERR 16
ERR 17
NOERR 18
NOERR 19
NOERR 20
ERR 21
NOERR 22
NOERR 23
NOERR 24
NOERR 25
NOERR 26
NOERR 27
NOERR 28
ERR 29
ERR 30
NOERR 31

/* Device IRQs have no CPU error code and return to the interrupted context. */
.macro IRQ line
.type irq_\line, @function
irq_\line:
    pushl $0
    pushl $(32 + \line)
    jmp interrupt_common
.size irq_\line, . - irq_\line
.endm
.irp line,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    IRQ \line
.endr

.type interrupt_common, @function
interrupt_common:
    cld
    pushal
    xor %eax, %eax
    mov %ds, %ax
    push %eax
    mov %es, %ax
    push %eax
    mov %fs, %ax
    push %eax
    mov %gs, %ax
    push %eax
    mov $KERNEL_DATA_SELECTOR, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* EBX is callee-saved by C. Retain the frame across stack alignment. */
    mov %esp, %ebx
    and $-16, %esp
    sub $12, %esp
    push %ebx
    call interrupt_dispatch
    mov %ebx, %esp
    /* A trusted caller may also supply a complete saved frame for first entry. */
.global interrupt_return
interrupt_return:
    pop %eax
    mov %ax, %gs
    pop %eax
    mov %ax, %fs
    pop %eax
    mov %ax, %es
    pop %eax
    mov %ax, %ds
    popal
    add $8, %esp
    iret
.size interrupt_common, . - interrupt_common

.section .rodata, "a"
.balign 4
.global exception_stub_table
.type exception_stub_table, @object
exception_stub_table:
.irp vector,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .long exception_\vector
.endr
.size exception_stub_table, . - exception_stub_table

.global irq_stub_table
.type irq_stub_table, @object
irq_stub_table:
.irp line,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    .long irq_\line
.endr
.size irq_stub_table, . - irq_stub_table

.section .note.GNU-stack, "", @progbits
