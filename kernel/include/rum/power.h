#ifndef RUM_POWER_H
#define RUM_POWER_H
#include <stdint.h>
#include <rum/fs.h>
enum power_action { POWER_SHUTDOWN, POWER_REBOOT };
/* Boot-only discovery, after the physical identity window is mapped. */
void power_initialize(uint32_t identity_limit);
/* Trusted foreground context only. Returns an error without powering off if
   processes cannot drain or a mounted volume cannot flush. */
enum fs_error system_power(enum power_action);
/* HAL entry: caller has drained userspace and flushed the volume. */
_Noreturn void power_machine(enum power_action);
#endif
