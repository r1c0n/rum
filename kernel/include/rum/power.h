#ifndef RUM_POWER_H
#define RUM_POWER_H
#include <stdint.h>
#include <rum/fs.h>
#include <rum/multiboot.h>
enum power_action { POWER_SHUTDOWN, POWER_REBOOT };
/* Boot-only discovery after validating the memory map, before paging. Only
   cached port numbers and sleep types survive; no firmware pointers are used. */
void power_initialize(const struct multiboot_info *);
/* Trusted foreground context only. Returns an error without powering off if
   processes cannot drain or a mounted volume cannot flush. */
enum fs_error system_power(enum power_action);
/* HAL entry: caller has drained userspace and flushed the volume. */
_Noreturn void power_machine(enum power_action);
#endif
