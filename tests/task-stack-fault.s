#include <rum/memory_layout.h>
.section .text, "ax"
.code32
.global task_trigger_stack_guard, task_stack_fault_instruction
.type task_trigger_stack_guard, @function
task_trigger_stack_guard:
    /* TSS.ESP0 is the current slot top. Move to its base, then make the first
       write land in the unmapped guard. #PF delivery cannot use this stack,
       forcing vector 8 through the independent hardware task gate. */
    mov rum_tss + 4, %esp
    sub $RUM_KERNEL_STACK_SIZE, %esp
    mov $0x646f7562, %eax
task_stack_fault_instruction:
    push %eax
    ud2
.size task_trigger_stack_guard, . - task_trigger_stack_guard
.section .note.GNU-stack, "", @progbits
