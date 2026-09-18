#include <rum/abi/syscall.h>
.section .text, "ax"
.code32
.global main, user_probe_registers
.type main, @function
main:
    mov %esp, %eax
    and $15, %eax
    mov %eax, user_entry_alignment
    pushfl
    popl user_entry_flags
    mov %ebp, user_entry_ebp
    jmp user_test_main
.size main, . - main

.type user_probe_registers, @function
user_probe_registers:
    push %ebx
    sub $8, %esp
    mov $0x55667788, %ebx
    push $0
    push $0
    push $0
    push $RUM_SYS_GETPID
    call rum_syscall3
    add $16, %esp
    cmp $42, %eax
    jne 1f
    cmp $0x55667788, %ebx
    jne 1f
    mov $1, %eax
    jmp 2f
1:  xor %eax, %eax
2:  add $8, %esp
    pop %ebx
    ret
.size user_probe_registers, . - user_probe_registers
.section .note.GNU-stack, "", @progbits
