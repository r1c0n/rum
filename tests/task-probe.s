.section .text, "ax"
.code32
.global task_test_entry, task_test_registers
.type task_test_entry, @function
task_test_entry:
    mov %esp, %eax
    and $15, %eax
    mov %eax, task_test_entry_alignment
    jmp task_test_worker
.size task_test_entry, . - task_test_entry

.type task_test_registers, @function
task_test_registers:
    push %ebp
    push %ebx
    push %esi
    push %edi
    sub $12, %esp
    mov %esp, (%esp)
    movl $16, 4(%esp)
    mov $0x55667788, %ebx
    mov $0x13579bdf, %esi
    mov $0x2468ace0, %edi
    mov $0x0badf00d, %ebp
1:
    call task_yield
    test %eax, %eax
    jz 2f
    cmp (%esp), %esp
    jne 2f
    cmp $0x55667788, %ebx
    jne 2f
    cmp $0x13579bdf, %esi
    jne 2f
    cmp $0x2468ace0, %edi
    jne 2f
    cmp $0x0badf00d, %ebp
    jne 2f
    decl 4(%esp)
    jnz 1b
    mov $1, %eax
    jmp 3f
2:
    xor %eax, %eax
3:
    add $12, %esp
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret
.size task_test_registers, . - task_test_registers
.section .bss, "aw", @nobits
.balign 4
.global task_test_entry_alignment
task_test_entry_alignment:
    .skip 4
.section .note.GNU-stack, "", @progbits
