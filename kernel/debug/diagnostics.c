#include <rum/cpu.h>
#include <rum/diagnostics.h>
#include <rum/gdt.h>
#include <rum/memory.h>

bool diagnostics_capture(struct kernel_diagnostics *result)
{
    if (!result) return false;
    uint32_t flags = cpu_interrupt_save();
    memset(result, 0, sizeof *result);
    result->flags = flags;
    __asm__ volatile ("mov %%cr0, %0; mov %%cr3, %1; mov %%esp, %2"
                      : "=r"(result->cr0), "=r"(result->cr3), "=r"(result->kernel_esp));
    result->esp0 = rum_tss.esp0;
    result->tasks_ready = task_snapshot_read(&result->tasks);
    result->paging = paging_stats();
    result->physical = pmm_stats();
    result->heap = heap_stats();
    result->files = ramfs_stats();
    cpu_interrupt_restore(flags);
    return true;
}
