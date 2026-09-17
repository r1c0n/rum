.section .text, "ax"
.code32
.global irq_probe
.type irq_probe, @function
irq_probe:
    pushal
    mov %esp, irq_expected_esp
    mov $0x11223344, %eax
    mov $0x55667788, %ebx
    mov $0x99AABBCC, %ecx
    mov $0xDDEEFF00, %edx
    mov $0x13579BDF, %esi
    mov $0x2468ACE0, %edi
    mov $0x0BADF00D, %ebp
    std
    int $0x27                   /* Spurious IRQ7; IRET must preserve context. */
    mov %eax, irq_observed
    mov %ebx, irq_observed + 4
    mov %ecx, irq_observed + 8
    mov %edx, irq_observed + 12
    mov %esi, irq_observed + 16
    mov %edi, irq_observed + 20
    mov %ebp, irq_observed + 24
    mov %esp, irq_observed + 28
    pushfl
    popl irq_observed_flags
    cld                         /* Return to C with its required direction. */
    int $0x2F                   /* Spurious slave IRQ15; master-only EOI. */
    popal
    ret
.size irq_probe, . - irq_probe

.section .bss, "aw", @nobits
.balign 4
.global irq_observed, irq_expected_esp, irq_observed_flags
irq_observed:
    .skip 32
irq_expected_esp:
    .skip 4
irq_observed_flags:
    .skip 4
.section .note.GNU-stack, "", @progbits
