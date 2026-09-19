#include <rum/abi/error.h>
#include <rum/abi/syscall.h>
#include <rum/memory_layout.h>

.set USER_DATA, RUM_USER_BASE + RUM_PAGE_SIZE
.set CROSS_BUFFER, USER_DATA + RUM_PAGE_SIZE - 64
.set READ_BUFFER, USER_DATA + 0x200
.set ERROR_BUFFER, USER_DATA + 0x300
.set USER_HOLE, USER_DATA + 2 * RUM_PAGE_SIZE

.section .rodata, "a"
.code32
.global syscall_probe_start, syscall_probe_end
syscall_probe_start:
    /* getpid and the full non-EAX preservation contract. */
    mov $0x11223344, %ebx
    mov $0x55667788, %ecx
    mov $0x99AABBCC, %edx
    mov $0x13579BDF, %esi
    mov $0x2468ACE0, %edi
    mov $0x0BADF00D, %ebp
    mov $RUM_SYS_GETPID, %eax
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 0
    cmpl $0x11223344, %ebx
    jne 1f
    cmpl $0x55667788, %ecx
    jne 1f
    cmpl $0x99AABBCC, %edx
    jne 1f
    cmpl $0x13579BDF, %esi
    jne 1f
    cmpl $0x2468ACE0, %edi
    jne 1f
    cmpl $0x0BADF00D, %ebp
    jne 1f
    movl $1, USER_DATA + 4
1:
    /* Unknown calls and handle validation. */
    mov $99, %eax
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 8
    mov $RUM_SYS_READ, %eax
    mov $RUM_STDOUT, %ebx
    mov $READ_BUFFER, %ecx
    mov $1, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 12
    mov $RUM_SYS_WRITE, %eax
    mov $RUM_STDIN, %ebx
    mov $USER_DATA, %ecx
    mov $1, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 16

    /* Zero-length calls do not inspect their otherwise invalid pointers. */
    mov $RUM_SYS_WRITE, %eax
    mov $RUM_STDOUT, %ebx
    mov $0xFFFFFFFF, %ecx
    xor %edx, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 20
    mov $RUM_SYS_READ, %eax
    mov $RUM_STDIN, %ebx
    mov $0xFFFFFFFF, %ecx
    xor %edx, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 24

    /* Unmapped, read-only, and overflowing ranges return EFAULT. */
    mov $RUM_SYS_WRITE, %eax
    mov $RUM_STDOUT, %ebx
    mov $USER_HOLE, %ecx
    mov $1, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 28
    mov $RUM_SYS_READ, %eax
    mov $RUM_STDIN, %ebx
    mov $USER_HOLE, %ecx
    mov $1, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 32
    mov $RUM_SYS_READ, %eax
    mov $RUM_STDIN, %ebx
    mov $RUM_USER_BASE, %ecx
    mov $1, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 36
    mov $RUM_SYS_WRITE, %eax
    mov $RUM_STDOUT, %ebx
    mov $(RUM_USER_END - 4), %ecx
    mov $8, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 40
    mov $RUM_SYS_READ, %eax
    mov $RUM_STDIN, %ebx
    mov $(RUM_USER_END - 4), %ecx
    mov $8, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 44

    /* A 192-byte request crosses a page and returns the 128-byte console chunk. */
    mov $RUM_SYS_WRITE, %eax
    mov $RUM_STDOUT, %ebx
    mov $CROSS_BUFFER, %ecx
    mov $192, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 48
    cmpl $RUM_STDOUT, %ebx
    jne 2f
    cmpl $CROSS_BUFFER, %ecx
    jne 2f
    cmpl $192, %edx
    jne 2f
    movl $1, USER_DATA + 52
2:
    mov $RUM_SYS_WRITE, %eax
    mov $RUM_STDERR, %ebx
    mov $ERROR_BUFFER, %ecx
    mov $3, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 56

    /* This call blocks until the QEMU test injects keyboard input. */
    mov $RUM_SYS_READ, %eax
    mov $RUM_STDIN, %ebx
    mov $READ_BUFFER, %ecx
    mov $16, %edx
    int $RUM_SYSCALL_VECTOR
    mov %eax, USER_DATA + 60
    movzbl READ_BUFFER, %eax
    mov %eax, USER_DATA + 64

    mov $RUM_SYS_EXIT, %eax
    mov $-37, %ebx
    int $RUM_SYSCALL_VECTOR
    movl $1, USER_DATA + 68
    ud2
syscall_probe_end:

.section .note.GNU-stack, "", @progbits
