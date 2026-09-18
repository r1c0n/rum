#include <rum/memory_layout.h>

/* GRUB finds this Multiboot v1 header within the first 8 KiB of the ELF. */
.set ALIGN, 1 << 0
.set MEMINFO, 1 << 1
.set FLAGS, ALIGN | MEMINFO
.set MAGIC, 0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.balign 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/* C needs a stack. x86 stacks grow downward; reserve 16 KiB. */
.section .bss, "aw", @nobits
.balign RUM_STACK_ALIGNMENT
.global __boot_stack_bottom, __boot_stack_top
__boot_stack_bottom:
.skip RUM_BOOT_STACK_SIZE
__boot_stack_top:

.section .text, "ax"
.code32
.global _start
.type _start, @function
_start:
    cli
    cld                         /* C string operations expect forward direction. */
    mov $__boot_stack_top, %esp
    xor %ebp, %ebp

    /* GRUB supplies magic in EAX and the boot information address in EBX.
       Reserve padding so ESP is 16-byte aligned immediately before CALL. */
    sub $8, %esp
    push %ebx                   /* Second C argument: boot information address. */
    push %eax                   /* First C argument: Multiboot magic. */
    /* Save the handoff before reloading AX and replace GRUB's segment table. */
    call gdt_initialize
    call kernel_main
    add $16, %esp

    /* The normal kernel never returns. Halt if startup fails. */
    jmp cpu_halt
.size _start, . - _start

.section .note.GNU-stack, "", @progbits
