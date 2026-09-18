#ifndef RUM_TEST_CPU_H
#define RUM_TEST_CPU_H
#include <stdint.h>
/* Host tests run without privileged instructions; production uses cli/popfl. */
static inline uint32_t cpu_interrupt_save(void) { return 0; }
static inline void cpu_interrupt_restore(uint32_t flags) { (void)flags; }
#endif
