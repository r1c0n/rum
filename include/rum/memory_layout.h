#ifndef RUM_MEMORY_LAYOUT_H
#define RUM_MEMORY_LAYOUT_H

/* Unsuffixed constants also work in preprocessed assembly and linker scripts.
   Address ranges use an exclusive end. A reservation does not create mappings. */
#define RUM_PAGE_SIZE              0x00001000
#define RUM_PAGE_TABLE_SPAN        0x00400000
#define RUM_STACK_ALIGNMENT        16
#define RUM_LOW_RESERVED_END       0x00100000
#define RUM_KERNEL_LOAD_BASE       0x00200000
#define RUM_IDENTITY_END           0x40000000
#define RUM_BOOT_STACK_SIZE         0x00004000

#define RUM_HEAP_BASE              RUM_IDENTITY_END
#define RUM_HEAP_END               0x40400000
#define RUM_KERNEL_ALIAS_BASE      RUM_HEAP_END
#define RUM_KERNEL_STACK_BASE      0x7FC00000
#define RUM_KERNEL_STACK_END       0x80000000
#define RUM_KERNEL_STACK_SIZE      0x00004000
#define RUM_KERNEL_STACK_STRIDE    (RUM_PAGE_SIZE + RUM_KERNEL_STACK_SIZE)

#define RUM_USER_BASE              RUM_KERNEL_STACK_END
#define RUM_USER_PROGRAM_END       0xBFC00000
#define RUM_USER_STACK_WINDOW_BASE RUM_USER_PROGRAM_END
#define RUM_USER_END               0xC0000000
#define RUM_USER_STACK_TOP         RUM_USER_END
#define RUM_USER_STACK_SIZE        0x00010000
#define RUM_USER_STACK_BASE        (RUM_USER_STACK_TOP - RUM_USER_STACK_SIZE)
#define RUM_USER_STACK_GUARD_BASE  (RUM_USER_STACK_BASE - RUM_PAGE_SIZE)

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(RUM_LOW_RESERVED_END < RUM_KERNEL_LOAD_BASE &&
               RUM_KERNEL_LOAD_BASE < RUM_IDENTITY_END, "kernel fits identity window");
_Static_assert(RUM_HEAP_BASE < RUM_HEAP_END &&
               RUM_HEAP_END <= RUM_KERNEL_STACK_BASE &&
               RUM_KERNEL_STACK_BASE < RUM_KERNEL_STACK_END, "ordered kernel ranges");
_Static_assert(RUM_USER_BASE < RUM_USER_PROGRAM_END &&
               RUM_USER_PROGRAM_END <= RUM_USER_STACK_GUARD_BASE &&
               RUM_USER_STACK_GUARD_BASE < RUM_USER_STACK_BASE &&
               RUM_USER_STACK_BASE < RUM_USER_STACK_TOP, "ordered user ranges");
_Static_assert((RUM_KERNEL_LOAD_BASE % RUM_PAGE_SIZE) == 0 &&
               (RUM_LOW_RESERVED_END % RUM_PAGE_SIZE) == 0, "page-aligned boot ranges");
_Static_assert((RUM_IDENTITY_END % RUM_PAGE_TABLE_SPAN) == 0 &&
               (RUM_HEAP_END % RUM_PAGE_TABLE_SPAN) == 0 &&
               (RUM_KERNEL_STACK_BASE % RUM_PAGE_TABLE_SPAN) == 0 &&
               (RUM_KERNEL_STACK_END % RUM_PAGE_TABLE_SPAN) == 0 &&
               (RUM_USER_PROGRAM_END % RUM_PAGE_TABLE_SPAN) == 0 &&
               (RUM_USER_END % RUM_PAGE_TABLE_SPAN) == 0, "separate page-table windows");
_Static_assert(RUM_BOOT_STACK_SIZE > 0 && RUM_KERNEL_STACK_SIZE > 0 &&
               RUM_USER_STACK_SIZE > 0 &&
               (RUM_BOOT_STACK_SIZE % RUM_PAGE_SIZE) == 0 &&
               (RUM_KERNEL_STACK_SIZE % RUM_PAGE_SIZE) == 0 &&
               (RUM_USER_STACK_SIZE % RUM_PAGE_SIZE) == 0, "whole-page stacks");
_Static_assert((RUM_PAGE_SIZE % RUM_STACK_ALIGNMENT) == 0, "stack alignment");

/* The start must be inside [base, end), even for a zero-byte range. Subtraction
   avoids address + bytes overflow. This checks layout, not page permissions. */
static inline bool memory_range_contains(uint32_t base, uint32_t end,
                                         uint32_t address, size_t bytes)
{
    return address >= base && address < end && bytes <= end - address;
}

/* Today's alias API may use the heap and general kernel alias window only.
   Stack and user mappings will require their own ownership-aware APIs. */
static inline bool memory_kernel_mapping_range(uint32_t address, size_t bytes)
{
    return memory_range_contains(RUM_HEAP_BASE, RUM_KERNEL_STACK_BASE, address, bytes);
}
#endif

#endif
