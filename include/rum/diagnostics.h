#ifndef RUM_DIAGNOSTICS_H
#define RUM_DIAGNOSTICS_H

#include <rum/task.h>
#include <rum/heap.h>
#include <rum/pmm.h>
#include <rum/ramfs.h>

struct kernel_diagnostics {
    uint32_t cr0, cr3, kernel_esp, esp0, flags;
    bool tasks_ready;
    struct task_snapshot tasks;
    struct paging_statistics paging;
    struct pmm_statistics physical;
    struct heap_statistics heap;
    struct ramfs_statistics files;
};

/* Foreground or fatal exception only. No allocation, switching or cleanup.
   Capture preserves IF; output happens after the snapshot is complete. */
bool diagnostics_capture(struct kernel_diagnostics *result);
typedef void (*diagnostics_writer)(const char *text);
void diagnostics_render(const struct kernel_diagnostics *snapshot, diagnostics_writer write);
void diagnostics_print(void);
void diagnostics_panic(void);

#endif
