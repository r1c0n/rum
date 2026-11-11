#ifndef RUM_POWER_TEST_CPU_H
#define RUM_POWER_TEST_CPU_H
#include <stdint.h>
uint32_t cpu_interrupt_save(void);
_Noreturn void cpu_halt(void);
#endif
