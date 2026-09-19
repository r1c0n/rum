.section .text, "ax"
.code32

.macro TEST_REGISTERS
    mov %esp, fault_expected_esp
    mov $0x11223344, %eax
    mov $0x55667788, %ebx
    mov $0x99AABBCC, %ecx
    mov $0xDDEEFF00, %edx
    mov $0x13579BDF, %esi
    mov $0x2468ACE0, %edi
    mov $0x0BADF00D, %ebp
.endm

/* Symbols mark the actual faulting instructions for EIP assertions. */
.global fault_trigger_de, fault_de_instruction
.type fault_trigger_de, @function
fault_trigger_de:
    TEST_REGISTERS
    xor %ecx, %ecx
    xor %edx, %edx
fault_de_instruction:
    div %ecx
    jmp cpu_halt
.size fault_trigger_de, . - fault_trigger_de

.global fault_trigger_ud, fault_ud_instruction
.type fault_trigger_ud, @function
fault_trigger_ud:
    TEST_REGISTERS
    std                         /* Handler must clear DF before calling C. */
fault_ud_instruction:
    ud2
    jmp cpu_halt
.size fault_trigger_ud, . - fault_trigger_ud

.global fault_trigger_gp, fault_gp_instruction
.type fault_trigger_gp, @function
fault_trigger_gp:
    TEST_REGISTERS
    mov $0x30, %ax               /* Beyond our six-entry GDT. */
fault_gp_instruction:
    mov %ax, %ds
    jmp cpu_halt
.size fault_trigger_gp, . - fault_trigger_gp

.global fault_trigger_pf, fault_pf_instruction
.type fault_trigger_pf, @function
fault_trigger_pf:
    TEST_REGISTERS
fault_pf_instruction:
    mov 0x00400000, %eax         /* Unmapped in the isolated paging fixture. */
    jmp cpu_halt
.size fault_trigger_pf, . - fault_trigger_pf

.section .bss, "aw", @nobits
.balign 4
.global fault_expected_esp
fault_expected_esp:
    .skip 4

.section .note.GNU-stack, "", @progbits
