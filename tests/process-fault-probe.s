#include <rum/memory_layout.h>

.section .rodata, "a"
.code32

.macro SENTINELS
    mov $0x11223344, %ebx
    mov $0x55667788, %ecx
    mov $0x99AABBCC, %edx
    mov $0x13579BDF, %edi
    mov $0x2468ACE0, %ebp
.endm

.global process_fault_null_start, process_fault_null_instruction, process_fault_null_end
process_fault_null_start:
    SENTINELS
    xor %eax, %eax
process_fault_null_instruction:
    mov (%eax), %eax
    ud2
process_fault_null_end:

.global process_fault_kernel_start, process_fault_kernel_instruction, process_fault_kernel_end
process_fault_kernel_start:
    SENTINELS
    mov $RUM_KERNEL_LOAD_BASE, %eax
process_fault_kernel_instruction:
    mov (%eax), %eax
    ud2
process_fault_kernel_end:

.global process_fault_readonly_start, process_fault_readonly_instruction, process_fault_readonly_end
process_fault_readonly_start:
    SENTINELS
    mov $RUM_USER_BASE, %eax
process_fault_readonly_instruction:
    movl $0xDEADBEEF, (%eax)
    ud2
process_fault_readonly_end:

.global process_fault_ud2_start, process_fault_ud2_instruction, process_fault_ud2_end
process_fault_ud2_start:
    SENTINELS
process_fault_ud2_instruction:
    ud2
process_fault_ud2_end:

.global process_fault_privileged_start, process_fault_privileged_instruction, process_fault_privileged_end
process_fault_privileged_start:
    SENTINELS
process_fault_privileged_instruction:
    cli
    ud2
process_fault_privileged_end:

.global process_fault_io_start, process_fault_io_instruction, process_fault_io_end
process_fault_io_start:
    SENTINELS
    mov $0x5A, %eax
process_fault_io_instruction:
    outb %al, $0x80
    ud2
process_fault_io_end:

/* The fixture IRQ handlers update two words in the writable data page only
   while this process owns the active CR3. Reaching UD2 therefore proves that
   both device interrupts returned to the interrupted CPL-3 context. */
.global process_fault_irq_start, process_fault_irq_instruction, process_fault_irq_end
process_fault_irq_start:
    SENTINELS
    mov $(RUM_USER_BASE + RUM_PAGE_SIZE), %esi
1:
    cmpl $3, (%esi)
    jb 1b
    cmpl $1, 4(%esi)
    jb 1b
process_fault_irq_instruction:
    ud2
process_fault_irq_end:

.section .note.GNU-stack, "", @progbits
