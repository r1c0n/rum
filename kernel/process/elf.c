#include <stdint.h>
#include <rum/abi/elf.h>
#include <rum/abi/layout.h>
#include <rum/cpu_policy.h>
#include <rum/cpu.h>
#include <rum/elf.h>
#include <rum/elf-image.h>
#include <rum/abi/error.h>
#include <rum/memory.h>
#include <rum/memory_layout.h>
#include <rum/paging.h>
#include <rum/process_limits.h>

#define STACK_PAGES (RUM_USER_STACK_SIZE / RUM_PAGE_SIZE)

bool elf_arguments_valid(const struct rum_arguments *arguments)
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

static rum_result_t read_exact(const struct elf_source *source, uint32_t offset,
                               void *buffer, uint32_t bytes)
{
    if (offset > source->bytes || bytes > source->bytes - offset) return -RUM_ENOEXEC;
    unsigned char *out = buffer;
    while (bytes) {
        uint32_t chunk = bytes < ELF_READ_CHUNK ? bytes : ELF_READ_CHUNK;
        rum_result_t result = source->read(source->context, offset, out, chunk);
        if (result < 0) return result;
        if (!result || (uint32_t)result > chunk) return -RUM_EIO;
        offset += (uint32_t)result; out += result; bytes -= (uint32_t)result;
    }
    return 0;
}

static rum_result_t plan_image(const struct elf_source *source, struct rum_elf_plan *plan)
{
    if (source->bytes < sizeof(struct rum_elf_header) || source->bytes > RUM_ELF_IMAGE_LIMIT) return -RUM_ENOEXEC;
    struct rum_elf_header header;
    rum_result_t result = read_exact(source, 0, &header, sizeof header);
    if (result < 0) return result;
    if (!rum_elf_header_valid(&header, source->bytes, RUM_ELF_IMAGE_LIMIT)) return -RUM_ENOEXEC;
    struct rum_elf_program programs[RUM_ELF_PROGRAM_LIMIT];
    result = read_exact(source, header.phoff, programs, header.phnum * sizeof programs[0]);
    if (result < 0) return result;
    return rum_elf_make_plan(&header, programs, source->bytes, RUM_ELF_IMAGE_LIMIT, plan) ? 0 : -RUM_ENOEXEC;
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

rum_result_t elf_load_process_from_source(const struct elf_source *source,
                                         const struct rum_arguments *arguments,
                                         struct task_process *process)
{
    if (!process) return -RUM_EINVAL;
    *process = (struct task_process){0};
    if (!source || !source->read || !elf_arguments_valid(arguments)) return -RUM_EINVAL;
    struct rum_elf_plan plan;
    rum_result_t result = plan_image(source, &plan);
    if (result < 0) return result;

    struct paging_space *space = paging_space_create();
    if (!space) return -RUM_ENOMEM;
    unsigned char buffer[ELF_READ_CHUNK];
    for (unsigned i = 0; !result && i < plan.count; ++i) {
        const struct rum_elf_segment *segment = &plan.segments[i];
        if (!paging_user_allocate(space, segment->start, segment->pages, PAGING_WRITABLE)) { result = -RUM_ENOMEM; break; }
        uint32_t done = 0;
        while (!result && done < segment->program.file_bytes) {
            uint32_t bytes = segment->program.file_bytes - done;
            if (bytes > sizeof buffer) bytes = sizeof buffer;
            result = read_exact(source, segment->program.offset + done, buffer, bytes);
            if (!result && !paging_copy_to_user(space, segment->program.address + done, buffer, bytes)) result = -RUM_EIO;
            done += bytes;
        }
        if (!result && !(segment->program.flags & RUM_ELF_PF_W) &&
            !paging_user_protect(space, segment->start, segment->pages, 0)) result = -RUM_EIO;
    }
    uint32_t stack = 0;
    if (!result && !paging_user_allocate(space, RUM_USER_STACK_BASE, STACK_PAGES, PAGING_WRITABLE)) result = -RUM_ENOMEM;
    if (!result && !build_stack(space, arguments, &stack)) result = -RUM_EIO;
    if (result) {
        if (!paging_space_destroy(space)) cpu_halt();
        return result;
    }

    process->space = space;
    cpu_user_frame_initialize(&process->user_frame, plan.entry, stack);
    return 0;
}

static rum_result_t read_memory(void *context, uint32_t offset, void *out, uint32_t bytes)
{
    memcpy(out, (const unsigned char *)context + offset, bytes);
    return (rum_result_t)bytes;
}

bool elf_load_process(const void *image, size_t image_bytes,
                      const struct rum_arguments *arguments, struct task_process *process)
{
    const struct elf_source source = { .bytes = image_bytes, .context = (void *)image, .read = image ? read_memory : NULL };
    return elf_load_process_from_source(&source, arguments, process) == 0;
}

