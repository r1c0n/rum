#ifndef RUM_ELF_H
#define RUM_ELF_H
#include <rum/abi/elf.h>

#include <stdbool.h>
#include <stddef.h>
#include <rum/abi/process.h>
#include <rum/task.h>

/* Validate and prepare a static i386 ELF process without publishing it. On
   success the caller owns process->space and must transfer it to
   task_create_process or destroy it. Failure leaves process empty. */
bool elf_load_process(const void *image, size_t image_bytes,
                      const struct rum_arguments *arguments,
                      struct task_process *process);
/* Trusted source: positional reads return a byte count or a negative public
   error. Short reads are retried; EOF before the declared span is EIO. The
   loader requests at most ELF_READ_CHUNK bytes, and never takes ownership of
   the source. Keep its identity retained until this call returns. */
#define ELF_READ_CHUNK 512u
struct elf_source {
    uint64_t bytes;
    void *context;
    rum_result_t (*read)(void *, uint32_t offset, void *, uint32_t bytes);
};
/* Zero on success, ENOEXEC for invalid ELF, ENOMEM for construction failure,
   or the read error. Failure leaves an empty process and no owned mappings. */
rum_result_t elf_load_process_from_source(const struct elf_source *, const struct rum_arguments *,
                                         struct task_process *);
bool elf_arguments_valid(const struct rum_arguments *);

#endif
