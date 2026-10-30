#include <rum/memory_layout.h>
#include <rum/abi/syscall.h>

#define DATA (RUM_USER_BASE + RUM_PAGE_SIZE)
#define SPINS 100000

.section .rodata, "a"
.code32
.global preemption_probe_start, preemption_probe_end
preemption_probe_start:
    std
1:
    /* Neither program yields or asks the kernel to report progress. Every
       register except LOOP's counter has a process-specific sentinel. */
    cmp DATA + 16, %eax
    jne 9f
    cmp DATA + 20, %ebx
    jne 9f
    cmp DATA + 24, %edx
    jne 9f
    cmp DATA + 28, %esi
    jne 9f
    cmp DATA + 32, %edi
    jne 9f
    cmp DATA + 36, %ebp
    jne 9f
    cmp DATA + 40, %esp
    jne 9f
    mov %ds, DATA + 48
    mov %es, DATA + 52
    mov %fs, DATA + 56
    mov %gs, DATA + 60
    mov %ss, DATA + 64
    cmpw $0x23, DATA + 48
    jne 9f
    cmpw $0x23, DATA + 52
    jne 9f
    cmpw $0x23, DATA + 56
    jne 9f
    cmpw $0x23, DATA + 60
    jne 9f
    cmpw $0x23, DATA + 64
    jne 9f
    incl DATA
    /* LOOP and interrupt entry/return must preserve CF/DF/IF. Check those
       flags after a long window in which real PIT interrupts can preempt. */
    mov $SPINS, %ecx
    stc
2:
    loop 2b
    pushfl
    popl DATA + 44
    andl $0x27703, DATA + 44 /* CF, reserved bit, TF, IF, DF, IOPL, NT, VM. */
    cmpl $0x603, DATA + 44
    jne 9f
    cmpl $0, DATA + 4
    je 1b
    cmpl $1, DATA + 4
    je 3f
    cmpl $2, DATA + 4
    je 8f
    cmpl $3, DATA + 4
    je 4f
    cmpl $4, DATA + 4
    je 5f
    cmpl $5, DATA + 4
    je 6f
    cmpl $6, DATA + 4
    je 7f
    cmpl $7, DATA + 4
    je 10f
    cmpl $8, DATA + 4
    je 11f
    cmpl $9, DATA + 4
    je 12f
    cmpl $10, DATA + 4
    je 13f
    /* Test-only synchronous syscall: the fixture waits for PIT ticks while
       holding temporary resources, then returns through production assembly. */
    mov $0xfffff123, %eax
    int $RUM_SYSCALL_VECTOR
    cmpl $0, %eax
    jne 9f
3:
    mov $RUM_SYS_EXIT, %eax
    mov $37, %ebx
    int $RUM_SYSCALL_VECTOR
9:
    movl $1, DATA + 8
8:
    ud2
4:
    cli
    jmp 9b
5:
    outb %al, $0x80
    jmp 9b
6:
    fldz
    jmp 9b
7:
    pxor %mm0, %mm0
    jmp 9b
10:
    xorps %xmm0, %xmm0
    jmp 9b
11:
    mov RUM_KERNEL_LOAD_BASE, %eax
    jmp 9b
12:
    movl $0, RUM_USER_BASE
    jmp 9b
13:
    mov RUM_USER_STACK_GUARD_BASE, %eax
    jmp 9b
preemption_probe_end:
.section .note.GNU-stack, "", @progbits
