#ifndef RUM_SYSCALL_H
#define RUM_SYSCALL_H

#include <rum/interrupts.h>

/* Dispatch a validated ring-3 int 0x80 frame. EAX is the only register result. */
void syscall_dispatch(struct exception_frame *frame);

#endif
