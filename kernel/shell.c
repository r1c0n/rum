#include <stdbool.h>
#include <stddef.h>
#include <rum/heap.h>
#include <rum/diagnostics.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/snake.h>
#include <rum/terminal.h>

static char line[SHELL_LINE_CAPACITY];
static size_t length;
static shell_launcher launch_program;

static void print(const char *text)
{
    terminal_writestring(text);
    serial_writestring(text);
}

static bool equal(const char *left, const char *right)
{
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool whitespace(char character)
{
    return character == ' ' || character == '\t';
}

static void number(size_t value)
{
    char text[3 * sizeof(size_t) + 1];
    size_t length = 0;
    do { text[length++] = (char)('0' + value % 10); value /= 10; } while (value);
    text[length] = '\0';
    for (size_t i = 0; i < length / 2; ++i) {
        char c = text[i]; text[i] = text[length - i - 1]; text[length - i - 1] = c;
    }
    print(text);
}

static void signed_number(rum_result_t value)
{
    if (value < 0) print("-");
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    number(magnitude);
}

static void hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[11] = "0x00000000";
    for (unsigned i = 0; i < 8; ++i)
        text[2 + i] = digits[(value >> (28 - i * 4)) & 0xF];
    print(text);
}

static char *take_word(char **arguments)
{
    char *word = *arguments, *rest = word;
    while (*rest && !whitespace(*rest)) ++rest;
    if (*rest) *rest++ = '\0';
    while (whitespace(*rest)) ++rest;
    *arguments = rest;
    return word;
}

static bool list_file(const char *name, size_t size, void *context)
{
    (void)context;
    print("  "); print(name); print("  "); number(size); print(" bytes\n");
    return true;
}

static void execute(void)
{
    char *command = line;
    while (whitespace(*command))
        ++command;
    if (!*command)
        return;
    char *arguments = command;
    while (*arguments && !whitespace(*arguments))
        ++arguments;
    if (*arguments)
        *arguments++ = '\0';
    while (whitespace(*arguments))
        ++arguments;

    if (equal(command, "help")) {
        if (*arguments) {
            print("Usage: help\n");
            return;
        }
        print("Commands:\n"
              "  help         Show this list.\n"
              "  clear        Clear the console.\n"
              "  about        About rum.\n"
              "  echo <text>  Print text.\n"
              "  ls           List RAM files.\n"
              "  cat <name>   Read a file.\n"
              "  write <name> [text]  Create or replace a file.\n"
              "  rm <name>    Remove a file.\n"
              "  mem          Show heap and file usage.\n"
              "  diag         Show task and memory diagnostics.\n"
              "  run <program> [args]  Run an embedded user program.\n"
              "  snake        Play ASCII Snake.\n");
    } else if (equal(command, "clear")) {
        if (*arguments) {
            print("Usage: clear\n");
            return;
        }
        terminal_clear();
        serial_writestring("\x1b[2J\x1b[H");
    } else if (equal(command, "about")) {
        if (*arguments) {
            print("Usage: about\n");
            return;
        }
        print("rum OS v0.3.0\n"
              "An island of our own. A hobby kernel in C and x86 assembly.\n"
              "32-bit x86 | GRUB Multiboot | PIC, PIT and PS/2\n");
    } else if (equal(command, "echo")) {
        print(arguments);
        print("\n");
    } else if (equal(command, "ls")) {
        if (*arguments) { print("Usage: ls\n"); return; }
        if (!ramfs_stats().files) print("No files.\n");
        else ramfs_list(list_file, NULL);
    } else if (equal(command, "cat") || equal(command, "rm")) {
        char *name = take_word(&arguments);
        if (!*name || *arguments) {
            print(equal(command, "cat") ? "Usage: cat <name>\n" : "Usage: rm <name>\n");
            return;
        }
        const unsigned char *data = NULL;
        size_t size = 0;
        bool success = equal(command, "cat") ? ramfs_read(name, &data, &size) : ramfs_remove(name);
        if (!success) { print("File not found: "); print(name); print("\n"); return; }
        if (equal(command, "cat")) {
            for (size_t i = 0; i < size; ++i) {
                unsigned char c = data[i];
                char text[2] = { (c == '\n' || c == '\t' || (c >= ' ' && c <= '~')) ? (char)c : '.', 0 };
                print(text);
            }
            if (!size || data[size - 1] != '\n') print("\n");
        }
    } else if (equal(command, "write")) {
        char *name = take_word(&arguments);
        if (!*name) { print("Usage: write <name> [text]\n"); return; }
        size_t size = 0;
        while (arguments[size]) ++size;
        if (!ramfs_put(name, arguments, size)) {
            print("Cannot write file: invalid name, limit reached, or out of memory.\n");
        }
    } else if (equal(command, "mem")) {
        if (*arguments) { print("Usage: mem\n"); return; }
        struct heap_statistics heap = heap_stats();
        struct ramfs_statistics files = ramfs_stats();
        print("Heap: "); number(heap.mapped_bytes); print(" bytes mapped, ");
        number(heap.used_bytes); print(" bytes used, "); number(heap.allocations); print(" allocations\n");
        print("RAM files: "); number(files.files); print(" files, "); number(files.bytes); print(" bytes\n");
    } else if (equal(command, "diag")) {
        if (*arguments) { print("Usage: diag\n"); return; }
        diagnostics_print();
    } else if (equal(command, "run")) {
        char *program = take_word(&arguments);
        if (!*program) { print("Usage: run <program> [args]\n"); return; }
        if (!launch_program) { print("User program launcher is unavailable.\n"); return; }
        struct process_result result;
        enum process_launch_error error = launch_program(program, arguments, &result);
        if (error == PROCESS_LAUNCH_INVALID_ARGUMENTS) {
            print("Cannot launch: too many or oversized arguments.\n");
        } else if (error == PROCESS_LAUNCH_EXECUTABLE) {
            print("Cannot launch "); print(program);
            print(": file is missing or is not a valid executable.\n");
        } else if (error == PROCESS_LAUNCH_RESOURCES) {
            print("Cannot launch "); print(program); print(": process resources unavailable.\n");
        } else if (error != PROCESS_LAUNCH_OK) {
            print("Cannot finish process cleanup.\n");
        } else {
            print("Process "); number(result.process_id);
            if (result.termination == TASK_TERMINATION_EXIT) {
                print(" exited with status "); signed_number(result.exit_status); print(".\n");
            } else if (result.termination == TASK_TERMINATION_CANCELLED) {
                print(" cancelled by Ctrl+C.\n");
            } else {
                print(" faulted: vector "); number(result.fault.vector);
                print(", error "); hex(result.fault.error);
                print(", eip "); hex(result.fault.instruction);
                print(", address "); hex(result.fault.address);
                print(", stack "); hex(result.fault.stack); print(".\n");
            }
        }
    } else if (equal(command, "snake")) {
        if (*arguments) { print("Usage: snake\n"); return; }
        (void)snake_start(timer_ticks());
    } else {
        print("Unknown command: ");
        print(command);
        print(". Type 'help'.\n");
    }
}

void shell_set_launcher(shell_launcher launch)
{
    launch_program = launch;
}

void shell_initialize(void)
{
    length = 0;
    line[0] = '\0';
    print("> ");
}

void shell_receive(char character)
{
    if (snake_active()) {
        snake_receive(character, timer_ticks());
        if (!snake_active()) shell_initialize();
        return;
    }
    if (character == '\n') {
        line[length] = '\0';
        print("\n");
        execute();
        length = 0;
        line[0] = '\0';
        if (!snake_active()) shell_initialize();
    } else if (character == '\b') {
        if (length) {
            --length;
            terminal_putchar('\b');
            serial_writestring("\b \b");
        }
    } else if (character == '\t') {
        /* Match the previous console's fixed four-space expansion. */
        for (unsigned i = 0; i < 4; ++i)
            shell_receive(' ');
    } else if (character >= ' ' && character <= '~' && length < sizeof line - 1) {
        line[length++] = character;
        terminal_putchar(character);
        serial_putchar(character);
    }
}

bool shell_tick_due(uint32_t ticks) { return snake_tick_due(ticks); }
void shell_tick(uint32_t ticks) { snake_tick(ticks); }
