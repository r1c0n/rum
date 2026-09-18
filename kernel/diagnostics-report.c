#include <rum/diagnostics.h>
#include <rum/serial.h>
#include <rum/terminal.h>

static void console(const char *text)
{
    terminal_writestring(text);
    serial_writestring(text);
}

static void decimal(diagnostics_writer write, size_t value)
{
    char text[3 * sizeof value + 1];
    size_t length = 0;
    do { text[length++] = (char)('0' + value % 10); value /= 10; } while (value);
    text[length] = '\0';
    for (size_t i = 0; i < length / 2; ++i) {
        char c = text[i]; text[i] = text[length - i - 1]; text[length - i - 1] = c;
    }
    write(text);
}

static void hex(diagnostics_writer write, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[11] = "0x00000000";
    for (unsigned i = 0; i < 8; ++i) text[2 + i] = digits[(value >> (28 - i * 4)) & 15];
    write(text);
}

static void field(diagnostics_writer write, const char *name, uint32_t value)
{
    write(" "); write(name); write("="); hex(write, value);
}

static const struct task_information *current(const struct kernel_diagnostics *snapshot)
{
    for (uint32_t i = 0; i < snapshot->tasks.count; ++i)
        if (snapshot->tasks.tasks[i].id == snapshot->tasks.current) return &snapshot->tasks.tasks[i];
    return NULL;
}

void diagnostics_render(const struct kernel_diagnostics *snapshot, diagnostics_writer write)
{
    if (!snapshot || !write) return;
    const struct task_information *task = current(snapshot);
    write("Task: "); decimal(write, snapshot->tasks.current);
    write(snapshot->tasks_ready ? " | CR3: " : " (scheduler uninitialized) | CR3: ");
    hex(write, snapshot->cr3); write(" | TSS.ESP0: "); hex(write, snapshot->esp0);
    write("\nKernel ESP: "); hex(write, snapshot->kernel_esp);
    write(" | CR0: "); hex(write, snapshot->cr0); write("\n");
    if (task) {
        write("Current stack: "); hex(write, task->stack_base); write(".."); hex(write, task->stack_top);
        write(" | record CR3: "); hex(write, task->directory); write("\n");
    }
    write("Paging: "); decimal(write, snapshot->paging.directory_pages); write(" directories, ");
    decimal(write, snapshot->paging.shared_table_pages); write(" shared tables | active: ");
    hex(write, snapshot->paging.active_directory); write("\n");
    write("Physical: "); decimal(write, snapshot->physical.free_pages); write(" free / ");
    decimal(write, snapshot->physical.managed_pages); write(" managed pages\n");
    write("Tasks: "); decimal(write, snapshot->tasks.count); write(" live, ");
    decimal(write, snapshot->tasks.stack_pages); write(" owned stack pages, ");
    decimal(write, snapshot->tasks.directory_pages); write(" owned directories\n");
    write("Lifecycle: "); decimal(write, snapshot->tasks.created); write(" created, ");
    decimal(write, snapshot->tasks.exited); write(" exited, "); decimal(write, snapshot->tasks.reaped);
    write(" reaped, "); decimal(write, snapshot->tasks.switches); write(" switches\n");
    write("Heap: "); decimal(write, snapshot->heap.used_bytes); write(" / ");
    decimal(write, snapshot->heap.mapped_bytes); write(" bytes, ");
    decimal(write, snapshot->heap.allocations); write(" allocations\nRAM files: ");
    decimal(write, snapshot->files.files); write(" files, "); decimal(write, snapshot->files.bytes);
    write(" bytes\n");
    static const char *const states[] = {"unused", "runnable", "running", "blocked", "exited"};
    for (uint32_t i = 0; i < snapshot->tasks.count; ++i) {
        const struct task_information *item = &snapshot->tasks.tasks[i];
        write("  #"); decimal(write, item->id); write(" ");
        write(item->state <= TASK_EXITED ? states[item->state] : "unknown");
        write(" stack="); hex(write, item->stack_base); write(".."); hex(write, item->stack_top);
        write(item->owns_stack ? " owned\n" : " borrowed\n");
        write("     CR3="); hex(write, item->directory);
        write(item->owns_space ? " owned\n" : " borrowed\n");
    }
}

void diagnostics_print(void)
{
    struct kernel_diagnostics snapshot;
    if (diagnostics_capture(&snapshot)) diagnostics_render(&snapshot, console);
}

void diagnostics_panic(void)
{
    struct kernel_diagnostics snapshot;
    if (!diagnostics_capture(&snapshot)) return;
    const struct task_information *task = current(&snapshot);
    console("\n  Task: "); decimal(console, snapshot.tasks.current);
    console("  CR3: "); hex(console, snapshot.cr3); console("  ESP0: "); hex(console, snapshot.esp0);
    console("\n  Kernel stack: "); hex(console, task ? task->stack_base : 0);
    console(".."); hex(console, task ? task->stack_top : 0);
    /* Serial retains the complete resource ledger without scrolling the fault
       registers off VGA. Prefix fields to keep them distinct from the frame. */
    serial_writestring("\nrum_diag_cpu");
    field(serial_writestring, "diag_task", snapshot.tasks.current);
    field(serial_writestring, "diag_cr3", snapshot.cr3);
    field(serial_writestring, "diag_recordcr3", task ? task->directory : 0);
    field(serial_writestring, "diag_activecr3", snapshot.paging.active_directory);
    field(serial_writestring, "diag_kesp", snapshot.kernel_esp);
    field(serial_writestring, "diag_esp0", snapshot.esp0);
    field(serial_writestring, "diag_base", task ? task->stack_base : 0);
    field(serial_writestring, "diag_top", task ? task->stack_top : 0);
    serial_writestring("\nrum_diag_resources");
    field(serial_writestring, "diag_live", snapshot.tasks.count);
    field(serial_writestring, "diag_stacks", snapshot.tasks.stack_pages);
    field(serial_writestring, "diag_owneddirs", snapshot.tasks.directory_pages);
    field(serial_writestring, "diag_dirs", snapshot.paging.directory_pages);
    field(serial_writestring, "diag_tables", snapshot.paging.shared_table_pages);
    field(serial_writestring, "diag_free", snapshot.physical.free_pages);
    field(serial_writestring, "diag_managed", snapshot.physical.managed_pages);
    field(serial_writestring, "diag_created", snapshot.tasks.created);
    field(serial_writestring, "diag_exited", snapshot.tasks.exited);
    field(serial_writestring, "diag_reaped", snapshot.tasks.reaped);
    serial_writestring("\n");
}
