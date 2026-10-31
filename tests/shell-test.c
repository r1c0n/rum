#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <rum/heap.h>
#include <rum/diagnostics.h>
#include "test-files.h"
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/terminal.h>
#include <rum/snake.h>
#include <rum/snake_model.h>
#include "page-backend.h"

static char output[16384];
static size_t output_length;
static uint32_t ticks;
uint32_t timer_ticks(void) { return ticks; }

static enum process_launch_error launch(const char *program, const char *arguments,
                                        struct process_result *result)
{
    *result = (struct process_result){ .process_id = 17 };
    if (!strcmp(program, "hello")) {
        assert(!strcmp(arguments, "one  two"));
        result->termination = TASK_TERMINATION_EXIT;
        result->exit_status = -42;
        return PROCESS_LAUNCH_OK;
    }
    if (!strcmp(program, "fault")) {
        result->termination = TASK_TERMINATION_FAULT;
        result->fault = (struct task_fault){
            .vector = 14, .error = 5, .address = 0x1234,
            .instruction = 0x10000000, .stack = 0xBFFFFFF0,
        };
        return PROCESS_LAUNCH_OK;
    }
    if (!strcmp(program, "spin")) {
        result->termination = TASK_TERMINATION_CANCELLED;
        return PROCESS_LAUNCH_OK;
    }
    if (!strcmp(program, "args")) return PROCESS_LAUNCH_INVALID_ARGUMENTS;
    if (!strcmp(program, "busy")) return PROCESS_LAUNCH_RESOURCES;
    if (!strcmp(program, "internal")) return PROCESS_LAUNCH_INTERNAL;
    return PROCESS_LAUNCH_EXECUTABLE;
}

/* Hardware capture is exercised by QEMU; host tests use the real formatter
   against a fixed snapshot and the real shell/console. */
bool diagnostics_capture(struct kernel_diagnostics *result)
{
    if (!result) return false;
    *result = (struct kernel_diagnostics){ .cr3 = 0x123000, .esp0 = 0x210000,
        .tasks_ready = true, .tasks = { .current = 7, .count = 2, .processes = 1,
            .stack_pages = 8, .directory_pages = 1, .user_table_pages = 2, .user_pages = 5,
            .tasks = {{ .id = 7, .state = TASK_RUNNING, .stack_base = 0x20C000,
                .stack_top = 0x210000, .directory = 0x123000, .owns_stack = true },
                { .id = 8, .process_id = 3, .parent = 7, .kind = TASK_PROCESS,
                  .state = TASK_EXITED, .termination = TASK_TERMINATION_CANCELLED,
                  .stack_base = 0x214000, .stack_top = 0x218000, .directory = 0x456000,
                  .resources = { .kernel_stack_pages = 4, .directory_pages = 1,
                                 .user_table_pages = 2, .user_pages = 5 },
                  .owns_space = true, .owns_stack = true }} } };
    result->heap = heap_stats();
    result->filesystem = fs_stats();
    return true;
}

/* Capture COM1 output; exercise the real shell and real VGA driver. */
void serial_putchar(char character)
{
    assert(output_length + 1 < sizeof output);
    output[output_length++] = character;
    output[output_length] = '\0';
}

void serial_writestring(const char *text)
{
    while (*text) {
        if (*text == '\n') serial_putchar('\r');
        serial_putchar(*text++);
    }
}

static void receive(const char *text)
{
    while (*text)
        shell_receive(*text++);
}

static void reset(void)
{
    output_length = 0;
    output[0] = '\0';
    terminal_initialize();
    terminal_status("uptime: 7s");
    shell_set_launcher(launch);
    shell_initialize();
}

int main(void)
{
    test_page_budget(-1);
    assert(mmap((void *)0xB8000, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
    terminal_initialize();
    assert(heap_initialize() && test_files_initialize() && test_files_mount());
    shell_set_launcher(launch); shell_initialize();
    reset(); receive("echo disk recovery\n");
    assert(strstr(output, "disk recovery\r\n> "));
    reset(); receive("write NOTE.TXT persistent\ncat NOTE.TXT\n");
    assert(strstr(output, "persistent\r\n> "));
    reset(); receive("ls /\n"); assert(strstr(output, "NOTE.TXT"));
    reset(); receive("rm NOTE.TXT\ncat NOTE.TXT\n");
    assert(strstr(output, "cat: not found"));
    reset(); receive("mem\ndiag\n"); assert(strstr(output, "Heap:") && strstr(output, "Filesystem:"));
    reset(); receive("run hello one  two\nrun fault\nrun spin\n");
    assert(strstr(output, "exited with status -42") && strstr(output, "faulted") && strstr(output, "cancelled by Ctrl+C"));
    reset(); receive("snake\npq"); assert(!snake_active());
    assert(fs_unmount_disk() == FS_OK);
    reset(); receive("ls\ncat NOTE.TXT\nwrite NOTE.TXT no\n");
    assert(strstr(output, "unavailable filesystem"));
    reset(); receive("echo no disk required\n"); assert(strstr(output, "no disk required\r\n> "));
    puts("PASS: disk recovery commands, diagnostics, launch, Snake and diskless console");
    return 0;
}
