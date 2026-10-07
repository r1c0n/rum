#ifndef RUM_TEST_ATA_CPU_H
#define RUM_TEST_ATA_CPU_H
#include <stdint.h>
uint32_t cpu_interrupt_save(void);
void cpu_interrupt_restore(uint32_t flags);
#endif
