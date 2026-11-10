#ifndef RUM_ELF_IMAGE_H
#define RUM_ELF_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <rum/abi/elf.h>

/* Pure executable policy shared by the kernel and SDK host validator. Input
   headers are trusted copies, never casts of unaligned or user-owned storage.
   Section headers are irrelevant to loading. A plan is usable only on success. */
struct rum_elf_segment {
    struct rum_elf_program program;
    uint32_t start, pages;
};
struct rum_elf_plan {
    uint32_t entry, count, pages;
    struct rum_elf_segment segments[RUM_ELF_PROGRAM_LIMIT];
};

static inline bool rum_elf_header_valid(const struct rum_elf_header *header,
                                       uint64_t bytes, uint64_t file_limit)
{
    static const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0, 0};
    if (!header || bytes < sizeof *header || bytes > file_limit) return false;
    for (size_t i = 0; i < sizeof ident; ++i) if (header->ident[i] != ident[i]) return false;
    uint64_t program_bytes = (uint64_t)header->phnum * sizeof(struct rum_elf_program);
    return header->type == RUM_ELF_EXEC && header->machine == RUM_ELF_I386 &&
        header->version == RUM_ELF_VERSION && !header->flags &&
        header->ehsize == sizeof *header && header->phentsize == sizeof(struct rum_elf_program) &&
        header->phnum && header->phnum <= RUM_ELF_PROGRAM_LIMIT &&
        header->phoff >= sizeof *header && header->phoff <= bytes &&
        program_bytes <= bytes - header->phoff;
}

static inline bool rum_elf_make_plan(const struct rum_elf_header *header,
                                    const struct rum_elf_program *programs,
                                    uint64_t bytes, uint64_t file_limit,
                                    struct rum_elf_plan *plan)
{
    if (!programs || !plan || !rum_elf_header_valid(header, bytes, file_limit)) return false;
    *plan = (struct rum_elf_plan){ .entry = header->entry,
        .pages = RUM_ABI_STACK_SIZE / RUM_ABI_PAGE_SIZE };
    bool entry = false, stack = false;
    uint32_t previous_end = RUM_ABI_PROGRAM_BASE;
    for (unsigned i = 0; i < header->phnum; ++i) {
        const struct rum_elf_program *program = &programs[i];
        if (program->type == RUM_ELF_PT_NULL) continue;
        if (program->type == RUM_ELF_PT_GNU_STACK) {
            if (stack || program->flags != (RUM_ELF_PF_R | RUM_ELF_PF_W) ||
                program->file_bytes || program->memory_bytes) return false;
            stack = true; continue;
        }
        if (program->type != RUM_ELF_PT_LOAD || program->file_bytes > program->memory_bytes ||
            program->memory_bytes > RUM_ELF_SEGMENT_MEMORY_LIMIT ||
            program->offset > bytes || program->file_bytes > bytes - program->offset ||
            !(program->flags & RUM_ELF_PF_R) || (program->flags & ~7u) ||
            (program->flags & (RUM_ELF_PF_W | RUM_ELF_PF_X)) == (RUM_ELF_PF_W | RUM_ELF_PF_X)) return false;
        uint32_t alignment = program->alignment;
        if ((alignment > 1 && ((alignment & (alignment - 1)) ||
             program->address % alignment != program->offset % alignment)) ||
            program->address % RUM_ABI_PAGE_SIZE != program->offset % RUM_ABI_PAGE_SIZE) return false;
        if (!program->memory_bytes) continue;
        if (program->address < RUM_ABI_PROGRAM_BASE || program->address >= RUM_ABI_PROGRAM_END ||
            program->memory_bytes > RUM_ABI_PROGRAM_END - program->address) return false;
        uint32_t start = program->address & ~(RUM_ABI_PAGE_SIZE - 1u);
        uint64_t end = ((uint64_t)program->address + program->memory_bytes + RUM_ABI_PAGE_SIZE - 1u) &
                       ~(uint64_t)(RUM_ABI_PAGE_SIZE - 1u);
        uint32_t pages = (uint32_t)((end - start) / RUM_ABI_PAGE_SIZE);
        if (end > RUM_ABI_PROGRAM_END || start < previous_end ||
            pages > RUM_ABI_MEMORY_BYTES / RUM_ABI_PAGE_SIZE - plan->pages) return false;
        previous_end = (uint32_t)end; plan->pages += pages;
        plan->segments[plan->count++] = (struct rum_elf_segment){ .program = *program, .start = start, .pages = pages };
        if ((program->flags & RUM_ELF_PF_X) && header->entry >= program->address &&
            header->entry - program->address < program->file_bytes) entry = true;
    }
    return entry && stack;
}
#endif
