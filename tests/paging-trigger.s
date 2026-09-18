.section .text, "ax"
.code32

.global paging_trigger_null, paging_null_instruction
.type paging_trigger_null, @function
paging_trigger_null:
paging_null_instruction:
    mov 0, %eax
    jmp cpu_halt
.size paging_trigger_null, . - paging_trigger_null

.global paging_trigger_text, paging_text_instruction
.type paging_trigger_text, @function
paging_trigger_text:
paging_text_instruction:
    movb $0, __text_start
    jmp cpu_halt
.size paging_trigger_text, . - paging_trigger_text

.global paging_trigger_rodata, paging_rodata_instruction
.type paging_trigger_rodata, @function
paging_trigger_rodata:
paging_rodata_instruction:
    movl $0, paging_readonly_word
    jmp cpu_halt
.size paging_trigger_rodata, . - paging_trigger_rodata

.global paging_trigger_unmapped, paging_unmapped_instruction
.type paging_trigger_unmapped, @function
paging_trigger_unmapped:
paging_unmapped_instruction:
    mov 0x40000000, %eax
    jmp cpu_halt
.size paging_trigger_unmapped, . - paging_trigger_unmapped

.global paging_trigger_readonly, paging_readonly_instruction
.type paging_trigger_readonly, @function
paging_trigger_readonly:
paging_readonly_instruction:
    movl $0, 0x40000000
    jmp cpu_halt
.size paging_trigger_readonly, . - paging_trigger_readonly

.section .note.GNU-stack, "", @progbits
