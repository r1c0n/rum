#ifndef RUM_ABI_LAYOUT_H
#define RUM_ABI_LAYOUT_H

/* Public virtual addresses; no private kernel structures or mappings. */
#define RUM_ABI_PAGE_SIZE        0x00001000
#define RUM_ABI_STACK_ALIGNMENT  16
#define RUM_ABI_PROGRAM_BASE     0x80000000
#define RUM_ABI_PROGRAM_END      0xBFC00000
#define RUM_ABI_STACK_TOP        0xC0000000
#define RUM_ABI_STACK_SIZE       0x00010000
#define RUM_ABI_MEMORY_BYTES     0x01000000 /* Program mappings plus user stack. */
#define RUM_ABI_STACK_BASE       (RUM_ABI_STACK_TOP - RUM_ABI_STACK_SIZE)

/* At first entry, ESP is 16-byte aligned and points at a 32-bit argc followed
   by argc user addresses, argv[argc] = 0, then an empty envp (one zero word).
   NUL-terminated strings follow in the same stack. No auxiliary vector. */
#define RUM_ABI_STACK_FIXED_WORDS 3 /* argc, argv terminator, envp terminator. */

#endif
