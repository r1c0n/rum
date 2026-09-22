#ifndef RUM_ELF_H
#define RUM_ELF_H

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

/* Load borrowed bytes from a RAM file through the same production path. */
bool elf_load_ramfs(const char *name, const struct rum_arguments *arguments,
                    struct task_process *process);

#endif
