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
.balign 16
stack_bottom:
.skip 16384
stack_top:

.section .text, "ax"
.code32
.global _start
.type _start, @function
_start:
    cli
    cld                         /* C string operations expect forward direction. */
    mov $stack_top, %esp
    xor %ebp, %ebp

    /* GRUB supplies magic in EAX and the boot information address in EBX.
       Reserve padding so ESP is 16-byte aligned immediately before CALL. */
    sub $8, %esp
    push %ebx                   /* Second C argument: boot information address. */
    push %eax                   /* First C argument: Multiboot magic. */
    call kernel_main
    add $16, %esp

    /* No interrupt handlers exist yet, so leave interrupts disabled. */
    cli
1:
    hlt
    jmp 1b
.size _start, . - _start

.section .note.GNU-stack, "", @progbits
