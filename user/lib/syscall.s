#include <rum/abi/syscall.h>

.section .text, "ax"
.code32
.global rum_syscall3
.type rum_syscall3, @function
rum_syscall3:
    push %ebx
    mov 8(%esp), %eax
    mov 12(%esp), %ebx
    mov 16(%esp), %ecx
    mov 20(%esp), %edx
    int $RUM_SYSCALL_VECTOR
    pop %ebx
    ret
.size rum_syscall3, . - rum_syscall3
.section .note.GNU-stack, "", @progbits
