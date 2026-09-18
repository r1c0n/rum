#ifndef RUM_ABI_ELF_H
#define RUM_ABI_ELF_H

#include <rum/abi/types.h>

/* Initial executable subset: static, little-endian ELF32 ET_EXEC for i386,
   no interpreter, dynamic linking or TLS. Program headers govern loading. */
#define RUM_ELF_EXEC 2
#define RUM_ELF_I386 3
#define RUM_ELF_VERSION 1
#define RUM_ELF_PROGRAM_LIMIT 16
#define RUM_ELF_PT_NULL 0
#define RUM_ELF_PT_LOAD 1
#define RUM_ELF_PT_GNU_STACK 0x6474E551
#define RUM_ELF_PF_X 1
#define RUM_ELF_PF_W 2
#define RUM_ELF_PF_R 4

#ifndef __ASSEMBLER__
struct rum_elf_header {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct rum_elf_program {
    uint32_t type, offset, address, physical, file_bytes, memory_bytes, flags, alignment;
};

_Static_assert(sizeof(struct rum_elf_header) == 52, "ELF32 file header");
_Static_assert(sizeof(struct rum_elf_program) == 32, "ELF32 program header");
#endif
#endif
