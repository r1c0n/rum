.section .text, "ax"
.code32
.global kernel_context_switch
.type kernel_context_switch, @function
kernel_context_switch:
    push %ebp
    push %ebx
    push %esi
    push %edi
    mov 20(%esp), %eax
    mov %esp, (%eax)
    mov 24(%esp), %esp
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    cld
    ret
.size kernel_context_switch, . - kernel_context_switch
.section .note.GNU-stack, "", @progbits
