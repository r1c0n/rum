#include <rum/memory_layout.h>

.section .text, "ax"
.code32
.global cpu_test_enter, interrupt_dispatch
.type cpu_test_enter, @function
cpu_test_enter:
    mov 4(%esp), %esp
    jmp interrupt_return
.size cpu_test_enter, . - cpu_test_enter

/* Record the ABI alignment at the exact C entry, before its prologue. */
.type interrupt_dispatch, @function
interrupt_dispatch:
    mov %esp, %eax
    and $15, %eax
    mov %eax, cpu_test_c_alignment
    jmp cpu_test_dispatch
.size interrupt_dispatch, . - interrupt_dispatch

.global cpu_test_user, cpu_test_wait_start, cpu_test_wait_end
.type cpu_test_user, @function
cpu_test_user:
    pushfl
    popl cpu_test_initial_flags
    std
cpu_test_wait_start:
    cmpl $3, cpu_test_irq_returns
    jb cpu_test_wait_start
cpu_test_wait_end:
    mov %eax, cpu_test_observed
    mov %ebx, cpu_test_observed + 4
    mov %ecx, cpu_test_observed + 8
    mov %edx, cpu_test_observed + 12
    mov %esi, cpu_test_observed + 16
    mov %edi, cpu_test_observed + 20
    mov %ebp, cpu_test_observed + 24
    mov %esp, cpu_test_observed + 28
    pushfl
    popl cpu_test_return_flags
    mov %ds, cpu_test_observed + 32
    mov %es, cpu_test_observed + 36
    mov %fs, cpu_test_observed + 40
    mov %gs, cpu_test_observed + 44
    mov %ss, cpu_test_observed + 48
    cmpl $1, cpu_test_operation
    je cpu_test_x87
    cmpl $2, cpu_test_operation
    je cpu_test_mmx
    cmpl $3, cpu_test_operation
    je cpu_test_sse
    cmpl $4, cpu_test_operation
    je cpu_test_io
.global cpu_test_done, cpu_test_x87, cpu_test_mmx, cpu_test_sse, cpu_test_io
cpu_test_done:
    ud2
cpu_test_x87:
    fldz
    jmp cpu_test_failed_instruction
cpu_test_mmx:
    pxor %mm0, %mm0
    jmp cpu_test_failed_instruction
cpu_test_sse:
    xorps %xmm0, %xmm0
    jmp cpu_test_failed_instruction
cpu_test_io:
    outb %al, $0x80
cpu_test_failed_instruction:
    /* An unsupported instruction that succeeds must not look like a pass. */
    ud2
.size cpu_test_user, . - cpu_test_user

.section .bss, "aw", @nobits
.balign RUM_STACK_ALIGNMENT
.global cpu_test_stack_bottom, cpu_test_stack_top, cpu_test_user_stack_top
cpu_test_stack_bottom:
    .skip RUM_BOOT_STACK_SIZE
cpu_test_stack_top:
    .skip RUM_PAGE_SIZE
cpu_test_user_stack_top:
.balign 4
.global cpu_test_observed, cpu_test_initial_flags, cpu_test_return_flags
.global cpu_test_c_alignment
cpu_test_observed:
    .skip 52
cpu_test_initial_flags:
    .skip 4
cpu_test_return_flags:
    .skip 4
cpu_test_c_alignment:
    .skip 4
.section .note.GNU-stack, "", @progbits
