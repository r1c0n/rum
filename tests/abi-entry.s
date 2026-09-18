#include <rum/memory_layout.h>
#include <rum/abi/syscall.h>
.section .text, "ax"
.code32
.global abi_test_enter, abi_test_syscall, interrupt_dispatch
.type abi_test_enter, @function
abi_test_enter:
    mov 4(%esp), %esp
    jmp interrupt_return
.size abi_test_enter, . - abi_test_enter
.type abi_test_syscall, @function
abi_test_syscall:
    pushl $0
    pushl $RUM_SYSCALL_VECTOR
    jmp interrupt_common
.size abi_test_syscall, . - abi_test_syscall
.type interrupt_dispatch, @function
interrupt_dispatch:
    mov %esp, %eax
    and $15, %eax
    mov %eax, abi_c_alignment
    jmp abi_test_dispatch
.size interrupt_dispatch, . - interrupt_dispatch

.section .bss, "aw", @nobits
.balign RUM_STACK_ALIGNMENT
.global cpu_test_stack_bottom, cpu_test_stack_top, abi_c_alignment
cpu_test_stack_bottom:
    .skip RUM_BOOT_STACK_SIZE
cpu_test_stack_top:
abi_c_alignment:
    .skip 4
.section .note.GNU-stack, "", @progbits
