#ifndef RUM_HANDLE_TEST_CPU_H
#define RUM_HANDLE_TEST_CPU_H
#include <stdint.h>
extern uint32_t handle_test_flags;
_Noreturn void cpu_halt(void);
static inline uint32_t cpu_interrupt_save(void)
{
    uint32_t flags = handle_test_flags; handle_test_flags &= ~0x200u; return flags;
}
static inline void cpu_interrupt_restore(uint32_t flags) { handle_test_flags = flags; }
#endif
