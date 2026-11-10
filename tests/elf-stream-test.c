/* Exercise the production loader; only address-space primitives are modeled. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rum/abi/error.h>
#include <rum/elf.h>
#include <rum/elf-image.h>
#include <rum/paging.h>

struct mapping { uint32_t address, pages, flags; unsigned char *data; };
struct paging_space { unsigned count; struct mapping mappings[RUM_ELF_PROGRAM_LIMIT + 1]; };
static unsigned spaces, pages, allocation_calls, fail_allocation, copy_calls, fail_copy, protections, fail_protect;
static bool deny(void) { return ++allocation_calls == fail_allocation; }
struct paging_space *paging_space_create(void)
{
    if (deny()) return NULL;
    struct paging_space *space = calloc(1, sizeof *space); assert(space); ++spaces; return space;
}
bool paging_space_destroy(struct paging_space *space)
{
    assert(space && spaces);
    for (unsigned i = 0; i < space->count; ++i) { pages -= space->mappings[i].pages; free(space->mappings[i].data); }
    free(space); --spaces; return true;
}
bool paging_user_allocate(struct paging_space *space, uint32_t address, uint32_t count, uint32_t flags)
{
    if (deny()) return false;
    assert(space->count < RUM_ELF_PROGRAM_LIMIT + 1);
    struct mapping *map = &space->mappings[space->count++];
    *map = (struct mapping){ address, count, flags, calloc(count, RUM_ABI_PAGE_SIZE) };
    assert(map->data); pages += count; return true;
}
static struct mapping *mapping(struct paging_space *space, uint32_t address, size_t bytes)
{
    for (unsigned i = 0; i < space->count; ++i) {
        struct mapping *map = &space->mappings[i]; size_t count = map->pages * RUM_ABI_PAGE_SIZE;
        if (address >= map->address && address - map->address <= count && bytes <= count - (address - map->address)) return map;
    }
    return NULL;
}
bool paging_copy_to_user(const struct paging_space *space, uint32_t address, const void *data, size_t bytes)
{
    if (++copy_calls == fail_copy) return false;
    struct mapping *map = mapping((struct paging_space *)space, address, bytes);
    assert(map && (map->flags & PAGING_WRITABLE)); memcpy(map->data + address - map->address, data, bytes); return true;
}
bool paging_user_protect(struct paging_space *space, uint32_t address, uint32_t count, uint32_t flags)
{
    if (++protections == fail_protect) return false;
    struct mapping *map = mapping(space, address, count * RUM_ABI_PAGE_SIZE);
    assert(map); map->flags = flags; return true;
}
_Noreturn void cpu_halt(void) { abort(); }

static unsigned char image[0x27000];
static const struct rum_arguments args = { .argc = 1, .string_bytes = 5, .strings = "test" };
struct input { unsigned reads, fail, short_limit; int error; uint32_t read_bytes; };
static struct rum_elf_header header;
static struct rum_elf_program programs[4];
static void fixture(void)
{
    memset(image, 0, sizeof image);
    header = (struct rum_elf_header){ .ident = {0x7f, 'E', 'L', 'F', 1, 1, 1}, .type = RUM_ELF_EXEC,
        .machine = RUM_ELF_I386, .version = RUM_ELF_VERSION, .entry = RUM_ABI_PROGRAM_BASE,
        .phoff = sizeof header, .ehsize = sizeof header, .phentsize = sizeof programs[0], .phnum = 4 };
    programs[0] = (struct rum_elf_program){ RUM_ELF_PT_LOAD, 0x1000, RUM_ABI_PROGRAM_BASE, 0, 4097, 8192, 5, 4096 };
    programs[1] = (struct rum_elf_program){ RUM_ELF_PT_LOAD, 0x4000, RUM_ABI_PROGRAM_BASE + 0x10000, 0, 131077, 131077, 4, 4096 };
    programs[2] = (struct rum_elf_program){ RUM_ELF_PT_LOAD, 0x26000, RUM_ABI_PROGRAM_BASE + 0x80000, 0, 2049, 393217, 6, 4096 };
    programs[3] = (struct rum_elf_program){ .type = RUM_ELF_PT_GNU_STACK, .flags = 6 };
    memcpy(image, &header, sizeof header); memcpy(image + header.phoff, programs, sizeof programs);
    for (unsigned i = 0; i < 3; ++i) for (uint32_t n = 0; n < programs[i].file_bytes; ++n)
        image[programs[i].offset + n] = (unsigned char)(n * 17 + i);
}
static rum_result_t read_image(void *context, uint32_t offset, void *out, uint32_t bytes)
{
    struct input *input = context; ++input->reads;
    assert(bytes && bytes <= ELF_READ_CHUNK);
    bool valid = offset < sizeof header && bytes <= sizeof header - offset;
    valid |= offset >= header.phoff && offset <= header.phoff + sizeof programs && bytes <= header.phoff + sizeof programs - offset;
    for (unsigned i = 0; i < 3; ++i)
        valid |= offset >= programs[i].offset && offset <= programs[i].offset + programs[i].file_bytes &&
                 bytes <= programs[i].offset + programs[i].file_bytes - offset;
    assert(valid); /* No reads of BSS, ignored file tails or gaps between LOADs. */
    if (input->reads == input->fail) return input->error;
    if (input->short_limit && bytes > input->short_limit) bytes = input->short_limit;
    memcpy(out, image + offset, bytes); input->read_bytes += bytes; return (rum_result_t)bytes;
}
static void clean_failure(struct elf_source *source, int error)
{
    struct task_process process; memset(&process, 0xa5, sizeof process);
    assert(elf_load_process_from_source(source, &args, &process) == error);
    assert(!memcmp(&process, &(struct task_process){0}, sizeof process) && !spaces && !pages);
}
static void inspect(struct task_process *process)
{
    for (unsigned i = 0; i < 3; ++i) {
        struct rum_elf_program *program = &programs[i];
        struct mapping *map = mapping(process->space, program->address, program->memory_bytes);
        assert(map && !!map->flags == !!(program->flags & RUM_ELF_PF_W));
        uint32_t prefix = program->address - map->address;
        for (uint32_t n = 0; n < prefix; ++n) assert(!map->data[n]);
        assert(!memcmp(map->data + prefix, image + program->offset, program->file_bytes));
        for (uint32_t n = prefix + program->file_bytes; n < map->pages * RUM_ABI_PAGE_SIZE; ++n) assert(!map->data[n]);
    }
    assert(process->user_frame.core.eip == header.entry && process->user_frame.core.eflags == 0x202 &&
           !mapping(process->space, RUM_ABI_STACK_BASE - RUM_ABI_PAGE_SIZE, 1));
    assert(mapping(process->space, RUM_ABI_STACK_BASE, RUM_ABI_STACK_SIZE));
}
static void malformed(void)
{
    struct rum_elf_plan plan;
    assert(rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    struct rum_elf_header bad = header;
    bad.phoff = UINT32_MAX - 8; assert(!rum_elf_make_plan(&bad, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    bad = header; bad.phnum = UINT16_MAX; assert(!rum_elf_make_plan(&bad, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    bad = header; bad.entry = programs[2].address; assert(!rum_elf_make_plan(&bad, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    struct rum_elf_program saved = programs[2];
    programs[2].memory_bytes = RUM_ELF_SEGMENT_MEMORY_LIMIT; /* individually bounded, collectively over quota */
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved; programs[2].address = RUM_ABI_PROGRAM_END - 4096;
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved; programs[2].address = programs[1].address + 4096;
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved; programs[2].alignment = 3;
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved; programs[2].memory_bytes = UINT32_MAX;
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved; programs[2].offset = UINT32_MAX;
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved; programs[2].flags = 7;
    assert(!rum_elf_make_plan(&header, programs, sizeof image, RUM_ELF_IMAGE_LIMIT, &plan));
    programs[2] = saved;
}
int main(void)
{
    fixture(); struct input input = {0};
    struct elf_source source = { sizeof image, &input, read_image };
    struct task_process process;
    assert(!elf_load_process_from_source(&source, &args, &process)); inspect(&process);
    unsigned reads = input.reads, copies = copy_calls, allocations = allocation_calls, protected = protections;
    assert(input.read_bytes == sizeof header + sizeof programs + programs[0].file_bytes + programs[1].file_bytes + programs[2].file_bytes);
    assert(paging_space_destroy(process.space));
    for (unsigned i = 1; i <= reads; ++i) {
        input = (struct input){ .fail = i, .error = -RUM_ETIMEDOUT }; clean_failure(&source, -RUM_ETIMEDOUT);
        input = (struct input){ .fail = i, .error = 0 }; clean_failure(&source, -RUM_EIO);
    }
    input = (struct input){ .fail = 1, .error = ELF_READ_CHUNK + 1 }; clean_failure(&source, -RUM_EIO);
    for (unsigned i = 1; i <= allocations; ++i) {
        allocation_calls = 0; fail_allocation = i; input = (struct input){0}; clean_failure(&source, -RUM_ENOMEM);
    }
    fail_allocation = 0;
    for (unsigned i = 1; i <= copies; ++i) {
        copy_calls = 0; fail_copy = i; input = (struct input){0}; clean_failure(&source, -RUM_EIO);
    }
    fail_copy = 0;
    for (unsigned i = 1; i <= protected; ++i) {
        protections = 0; fail_protect = i; input = (struct input){0}; clean_failure(&source, -RUM_EIO);
    }
    fail_protect = 0; input = (struct input){ .short_limit = 7 };
    assert(!elf_load_process_from_source(&source, &args, &process)); inspect(&process); assert(paging_space_destroy(process.space));
    input = (struct input){0}; source.bytes = RUM_ELF_IMAGE_LIMIT;
    assert(!elf_load_process_from_source(&source, &args, &process)); inspect(&process); assert(paging_space_destroy(process.space));
    source.bytes++; clean_failure(&source, -RUM_ENOEXEC);
    input = (struct input){0}; source.bytes = sizeof header - 1; clean_failure(&source, -RUM_ENOEXEC); assert(!input.reads);
    source.bytes = header.phoff + sizeof programs - 1; clean_failure(&source, -RUM_ENOEXEC);
    source.bytes = programs[2].offset + programs[2].file_bytes - 1; clean_failure(&source, -RUM_ENOEXEC);
    /* Congruent offsets are valid even when a LOAD starts inside a page. */
    header.entry += 37; programs[0].offset += 37; programs[0].address += 37;
    memcpy(image, &header, sizeof header); memcpy(image + header.phoff, programs, sizeof programs);
    source.bytes = sizeof image;
    assert(!elf_load_process_from_source(&source, &args, &process)); inspect(&process); assert(paging_space_destroy(process.space));
    fixture();
    malformed(); assert(!spaces && !pages);
    puts("PASS: streaming ELF, BSS, permissions, bounds and rollback at every read/copy/allocation stage"); return 0;
}
