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

    /* Future stack slots have one unmapped page followed by their payload.
       Every process gets the same user-stack addresses in its own directory. */
    for (uint32_t slot = 0; slot < RUM_KERNEL_STACK_SLOTS; ++slot) {
        uint32_t guard = RUM_KERNEL_STACK_BASE + slot * RUM_KERNEL_STACK_STRIDE;
        uint32_t base = guard + RUM_PAGE_SIZE;
        assert(memory_range_contains(RUM_KERNEL_STACK_BASE, RUM_KERNEL_STACK_END,
                                     base, RUM_KERNEL_STACK_SIZE));
        assert(!memory_kernel_mapping_range(guard, RUM_PAGE_SIZE));
        assert(!memory_kernel_mapping_range(base, RUM_KERNEL_STACK_SIZE));
    }
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
