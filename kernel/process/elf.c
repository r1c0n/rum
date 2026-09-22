#include <stdint.h>
#include <rum/abi/elf.h>
#include <rum/abi/layout.h>
#include <rum/cpu_policy.h>
#include <rum/elf.h>
#include <rum/memory.h>
#include <rum/memory_layout.h>
#include <rum/paging.h>
#include <rum/process_limits.h>
#include <rum/ramfs.h>

#define STACK_PAGES (RUM_USER_STACK_SIZE / RUM_PAGE_SIZE)

struct load_segment {
    struct rum_elf_program program;
    uint32_t start, pages;
};

struct load_plan {
    uint32_t entry;
    unsigned count;
    struct load_segment segments[RUM_ELF_PROGRAM_LIMIT];
};

static bool valid_arguments(const struct rum_arguments *arguments)
{
    if (!arguments || !arguments->argc ||
        arguments->argc > RUM_PROCESS_ARGUMENT_LIMIT || !arguments->string_bytes ||
        arguments->string_bytes > RUM_PROCESS_ARGUMENT_BYTES) return false;

    uint32_t cursor = 0;
    for (uint32_t i = 0; i < arguments->argc; ++i) {
        if (arguments->offsets[i] != cursor) return false;
        while (cursor < arguments->string_bytes && arguments->strings[cursor]) ++cursor;
        if (cursor == arguments->string_bytes) return false;
        ++cursor;
    }
    return cursor == arguments->string_bytes;
}

static bool plan_image(const unsigned char *image, size_t bytes, struct load_plan *plan)
{
    if (!image || !plan || bytes < sizeof(struct rum_elf_header) || bytes > RAMFS_FILE_LIMIT)
        return false;

    struct rum_elf_header header;
    memcpy(&header, image, sizeof header);
    static const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0, 0};
    size_t program_bytes = (size_t)header.phnum * sizeof(struct rum_elf_program);
    if (memcmp(header.ident, ident, sizeof ident) || header.type != RUM_ELF_EXEC ||
        header.machine != RUM_ELF_I386 || header.version != RUM_ELF_VERSION || header.flags ||
        header.ehsize != sizeof header || header.phentsize != sizeof(struct rum_elf_program) ||
        !header.phnum || header.phnum > RUM_ELF_PROGRAM_LIMIT ||
        header.phoff < sizeof header || header.phoff > bytes ||
        program_bytes > bytes - header.phoff) return false;

    *plan = (struct load_plan){ .entry = header.entry };
    bool entry = false, stack = false;
    uint32_t previous_end = RUM_USER_BASE;
    uint32_t pages = STACK_PAGES;
    for (uint32_t i = 0; i < header.phnum; ++i) {
        struct rum_elf_program program;
        memcpy(&program, image + header.phoff + i * sizeof program, sizeof program);
        if (program.type == RUM_ELF_PT_NULL) continue;
        if (program.type == RUM_ELF_PT_GNU_STACK) {
            if (stack || program.flags != (RUM_ELF_PF_R | RUM_ELF_PF_W) ||
                program.file_bytes || program.memory_bytes) return false;
            stack = true;
            continue;
        }
        if (program.type != RUM_ELF_PT_LOAD || program.file_bytes > program.memory_bytes ||
            program.offset > bytes || program.file_bytes > bytes - program.offset ||
            !(program.flags & RUM_ELF_PF_R) || (program.flags & ~7u) ||
            (program.flags & (RUM_ELF_PF_W | RUM_ELF_PF_X)) ==
                (RUM_ELF_PF_W | RUM_ELF_PF_X)) return false;
        if (!program.memory_bytes) continue;

        uint32_t alignment = program.alignment;
        if ((alignment > 1 && ((alignment & (alignment - 1)) ||
             program.address % alignment != program.offset % alignment)) ||
            program.address % RUM_PAGE_SIZE != program.offset % RUM_PAGE_SIZE ||
            program.address < RUM_USER_BASE || program.address >= RUM_USER_PROGRAM_END ||
            program.memory_bytes > RUM_USER_PROGRAM_END - program.address) return false;

        uint32_t start = program.address & ~(RUM_PAGE_SIZE - 1u);
        uint32_t end = (program.address + program.memory_bytes + RUM_PAGE_SIZE - 1u) &
                       ~(RUM_PAGE_SIZE - 1u);
        uint32_t count = (end - start) / RUM_PAGE_SIZE;
        if (start < previous_end || count > RUM_PROCESS_USER_BYTES / RUM_PAGE_SIZE - pages)
            return false;
        previous_end = end;
        pages += count;
        plan->segments[plan->count++] = (struct load_segment){
            .program = program, .start = start, .pages = count,
        };
        if ((program.flags & RUM_ELF_PF_X) && header.entry >= program.address &&
            header.entry - program.address < program.file_bytes) entry = true;
    }
    return entry && stack;
}

static bool build_stack(struct paging_space *space, const struct rum_arguments *arguments,
                        uint32_t *stack)
{
    uint32_t strings = RUM_USER_STACK_TOP - arguments->string_bytes;
    uint32_t words_count = arguments->argc + RUM_ABI_STACK_FIXED_WORDS;
    uint32_t words_bytes = words_count * sizeof(uint32_t);
    if (strings < RUM_USER_STACK_BASE + words_bytes) return false;
    uint32_t esp = (strings - words_bytes) & ~(RUM_STACK_ALIGNMENT - 1u);
    if (esp < RUM_USER_STACK_BASE) return false;

    uint32_t words[RUM_PROCESS_ARGUMENT_LIMIT + RUM_ABI_STACK_FIXED_WORDS] = {0};
    words[0] = arguments->argc;
    for (uint32_t i = 0; i < arguments->argc; ++i)
        words[i + 1] = strings + arguments->offsets[i];
    if (!paging_copy_to_user(space, strings, arguments->strings, arguments->string_bytes) ||
        !paging_copy_to_user(space, esp, words, words_bytes)) return false;
    *stack = esp;
    return true;
}

bool elf_load_process(const void *image, size_t image_bytes,
                      const struct rum_arguments *arguments,
                      struct task_process *process)
{
    if (!process) return false;
    *process = (struct task_process){0};
    struct load_plan plan;
    if (!valid_arguments(arguments) || !plan_image(image, image_bytes, &plan)) return false;

    struct paging_space *space = paging_space_create();
    if (!space) return false;
    const unsigned char *bytes = image;
    bool success = true;
    for (unsigned i = 0; success && i < plan.count; ++i) {
        const struct load_segment *segment = &plan.segments[i];
        success = paging_user_allocate(space, segment->start, segment->pages, PAGING_WRITABLE) &&
                  paging_copy_to_user(space, segment->program.address,
                                      bytes + segment->program.offset,
                                      segment->program.file_bytes);
        if (success && !(segment->program.flags & RUM_ELF_PF_W))
            success = paging_user_protect(space, segment->start, segment->pages, 0);
    }
    uint32_t stack = 0;
    if (success)
        success = paging_user_allocate(space, RUM_USER_STACK_BASE, STACK_PAGES, PAGING_WRITABLE) &&
                  build_stack(space, arguments, &stack);
    if (!success) {
        (void)paging_space_destroy(space);
        return false;
    }

    process->space = space;
    cpu_user_frame_initialize(&process->user_frame, plan.entry, stack);
    return true;
}

bool elf_load_ramfs(const char *name, const struct rum_arguments *arguments,
                    struct task_process *process)
{
    if (process) *process = (struct task_process){0};
    const unsigned char *image;
    size_t bytes;
    return process && ramfs_read(name, &image, &bytes) &&
           elf_load_process(image, bytes, arguments, process);
}
