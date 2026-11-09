#include <rum/abi/syscall.h>
.section .text
.global vm_release_on_stack
.type vm_release_on_stack, @function
vm_release_on_stack:
    push %ebx
    push %esi
    mov %esp, %esi
    mov 16(%esi), %ebx
    mov 12(%esi), %esp
    mov $RUM_SYS_VM_RELEASE, %eax
    xor %ecx, %ecx
    xor %edx, %edx
    int $RUM_SYSCALL_VECTOR
    mov %esi, %esp
    pop %esi
    pop %ebx
    ret
.size vm_release_on_stack, .-vm_release_on_stack
.section .note.GNU-stack, "", @progbits
