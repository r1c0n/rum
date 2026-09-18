#include <rum/abi/layout.h>

.section .text.start, "ax"
.code32
.global _start
.type _start, @function
_start:
    cld
    xor %ebp, %ebp
    mov (%esp), %eax
    lea 4(%esp), %edx
    and $-RUM_ABI_STACK_ALIGNMENT, %esp
    sub $8, %esp
    push %edx
    push %eax
    call main
    add $16, %esp
    sub $12, %esp
    push %eax
    call rum_exit
    ud2
.size _start, . - _start
.section .note.GNU-stack, "", @progbits
