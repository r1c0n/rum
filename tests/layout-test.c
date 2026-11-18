#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <rum/process_limits.h>

int main(void)
{
    /* Complete ranges, empty ranges, and arithmetic that would wrap in 32 bits. */
    assert(memory_range_contains(0x1000, 0x3000, 0x1000, 0x2000));
    assert(memory_range_contains(0x1000, 0x3000, 0x2FFF, 1));
    assert(memory_range_contains(0x1000, 0x3000, 0x2FFF, 0));
    assert(!memory_range_contains(0x1000, 0x3000, 0x3000, 0));
    assert(!memory_range_contains(0x1000, 0x3000, 0x0FFF, 1));
    assert(!memory_range_contains(0x1000, 0x3000, 0x2FFF, 2));
    assert(!memory_range_contains(0x3000, 0x1000, 0x3000, 1));
    assert(!memory_range_contains(0x1000, 0x3000, 0x1000, SIZE_MAX));
    assert(!memory_range_contains(0x80000000, 0xC0000000, 0xBFFFF000, 0x50001000));

    assert(memory_kernel_mapping_range(RUM_HEAP_BASE, RUM_PAGE_SIZE));
    assert(memory_kernel_mapping_range(RUM_KERNEL_ALIAS_BASE, RUM_PAGE_SIZE));
    assert(memory_kernel_mapping_range(RUM_KERNEL_STACK_BASE - RUM_PAGE_SIZE, RUM_PAGE_SIZE));
    assert(!memory_kernel_mapping_range(RUM_HEAP_BASE - RUM_PAGE_SIZE, RUM_PAGE_SIZE));
    assert(!memory_kernel_mapping_range(RUM_KERNEL_STACK_BASE - 1, 2));
    assert(!memory_kernel_mapping_range(RUM_KERNEL_STACK_BASE, RUM_PAGE_SIZE));
    assert(!memory_kernel_mapping_range(RUM_USER_BASE, RUM_PAGE_SIZE));
    assert(!memory_kernel_mapping_range(RUM_USER_STACK_BASE, RUM_PAGE_SIZE));
    assert(!memory_kernel_mapping_range(RUM_USER_END, RUM_PAGE_SIZE));
    assert(!memory_kernel_mapping_range(0xFFFFF000, RUM_PAGE_SIZE));

    /* Kernel stacks occupy disjoint slots in the shared supervisor window. */
    for (uint32_t slot = 0; slot < RUM_KERNEL_STACK_SLOTS; ++slot) {
        uint32_t guard = RUM_KERNEL_STACK_BASE + slot * RUM_KERNEL_STACK_STRIDE;
        uint32_t base = guard + RUM_PAGE_SIZE;
        assert(memory_range_contains(RUM_KERNEL_STACK_BASE, RUM_KERNEL_STACK_END,
                                     base, RUM_KERNEL_STACK_SIZE));
        assert(!memory_kernel_mapping_range(guard, RUM_PAGE_SIZE));
        assert(!memory_kernel_mapping_range(base, RUM_KERNEL_STACK_SIZE));
    }
    uint32_t previous_tls = RUM_USER_END;
    for (uint32_t slot = 0; slot < RUM_PROCESS_THREAD_LIMIT; ++slot) {
        uint32_t top;
        assert(thread_stack_top(slot, &top) && top == previous_tls);
        uint32_t base = top - RUM_USER_STACK_SIZE;
        uint32_t guard = base - RUM_PAGE_SIZE;
        uint32_t tls = guard - RUM_THREAD_TLS_SIZE;
        assert(memory_range_contains(RUM_USER_STACK_WINDOW_BASE, RUM_USER_END,
                                     tls, RUM_USER_THREAD_STRIDE));
        assert(slot || (base == RUM_USER_STACK_BASE && guard == RUM_USER_STACK_GUARD_BASE));
        previous_tls = tls;
    }
    uint32_t unchanged = 123;
    assert(!thread_stack_top(RUM_PROCESS_THREAD_LIMIT, &unchanged));
    assert(!thread_stack_top(UINT32_MAX, &unchanged) && unchanged == 123);
    assert(!thread_stack_top(0, NULL));
    assert(memory_range_contains(RUM_USER_STACK_WINDOW_BASE, RUM_USER_END,
                                 RUM_USER_STACK_GUARD_BASE, RUM_PAGE_SIZE));
    assert(memory_range_contains(RUM_USER_STACK_BASE, RUM_USER_STACK_TOP,
                                 RUM_USER_STACK_BASE, RUM_USER_STACK_SIZE));
    assert(!memory_range_contains(RUM_USER_STACK_BASE, RUM_USER_STACK_TOP,
                                  RUM_USER_STACK_GUARD_BASE, RUM_PAGE_SIZE));
    assert(!memory_range_contains(RUM_USER_BASE, RUM_USER_PROGRAM_END,
                                  RUM_USER_STACK_BASE, RUM_USER_STACK_SIZE));
    puts("memory layout tests passed");
    return 0;
}
