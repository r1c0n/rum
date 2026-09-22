#include <assert.h>
#include <stdio.h>
#include <rum/abi/elf.h>
#include <rum/abi/error.h>
#include <rum/abi/layout.h>
#include <rum/abi/process.h>
#include <rum/abi/syscall.h>
#include <rum/process_limits.h>

int main(void)
{
    _Static_assert(RUM_SYS_COUNT == 4, "kernel-only services must not enter the user ABI");
    assert(RUM_ABI_PROGRAM_BASE == RUM_USER_BASE &&
           RUM_ABI_PROGRAM_END == RUM_USER_PROGRAM_END &&
           RUM_ABI_STACK_BASE == RUM_USER_STACK_BASE &&
           RUM_ABI_STACK_TOP == RUM_USER_STACK_TOP &&
           RUM_ABI_PAGE_SIZE == RUM_PAGE_SIZE &&
           RUM_ABI_STACK_ALIGNMENT == RUM_STACK_ALIGNMENT);
    assert(RUM_PROCESS_ARGUMENT_LIMIT == RUM_ABI_ARGUMENT_LIMIT &&
           RUM_PROCESS_ARGUMENT_BYTES == RUM_ABI_ARGUMENT_BYTES);
    size_t largest_stack = (RUM_ABI_ARGUMENT_LIMIT + RUM_ABI_STACK_FIXED_WORDS) *
                          sizeof(rum_address_t) + RUM_ABI_ARGUMENT_BYTES +
                          RUM_ABI_STACK_ALIGNMENT - 1;
    assert(largest_stack < RUM_ABI_STACK_SIZE);
    const unsigned numbers[] = {RUM_SYS_EXIT, RUM_SYS_READ, RUM_SYS_WRITE, RUM_SYS_GETPID};
    for (unsigned i = 0; i < RUM_SYS_COUNT; ++i) {
        assert(numbers[i] < RUM_SYS_COUNT);
        for (unsigned j = 0; j < i; ++j) assert(numbers[i] != numbers[j]);
    }
    rum_result_t error = -RUM_EFAULT;
    assert(error < 0 && (uint32_t)error == 0xFFFFFFFEu);
    assert((rum_result_t)RUM_ABI_PID_MAX > 0);
    assert(sizeof(struct rum_arguments) == 4232 && sizeof(struct rum_elf_header) == 52);
    puts("PASS: fixed-width user ABI, shared layout/limits and maximum argument stack");
    return 0;
}
