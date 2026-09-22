#ifndef RUM_ABI_SYSCALL_H
#define RUM_ABI_SYSCALL_H

#include <rum/abi/types.h>

#define RUM_ABI_VERSION 1
#define RUM_SYSCALL_VECTOR 0x80

/* EAX = number/result; EBX, ECX, EDX = up to three 32-bit arguments.
   Return preserves every general register except EAX. Flags are unspecified.
   The kernel validates all handles and user pointers before using them. */
#define RUM_SYS_EXIT   0 /* EBX: signed exit status; does not return. */
#define RUM_SYS_READ   1 /* EBX: handle, ECX: address, EDX: byte capacity. */
#define RUM_SYS_WRITE  2 /* EBX: handle, ECX: address, EDX: byte count. */
#define RUM_SYS_GETPID 3 /* No arguments; returns a positive process ID. */
#define RUM_SYS_COUNT  4
#define RUM_ABI_PID_MAX 0x7FFFFFFF /* Positive IDs fit the signed result type. */

#define RUM_STDIN  0
#define RUM_STDOUT 1
#define RUM_STDERR 2

#endif
