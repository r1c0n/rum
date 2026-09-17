#include <stdbool.h>
#include <stddef.h>
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/terminal.h>

static char line[SHELL_LINE_CAPACITY];
static size_t length;

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
              "  echo <text>  Print text.\n");
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
        print("rum OS v0.1.0\n"
              "An island of our own. A hobby kernel in C and x86 assembly.\n"
              "32-bit x86 | GRUB Multiboot | PIC, PIT and PS/2\n");
    } else if (equal(command, "echo")) {
        print(arguments);
        print("\n");
    } else {
        print("Unknown command: ");
        print(command);
        print(". Type 'help'.\n");
    }
}

void shell_initialize(void)
{
    length = 0;
    line[0] = '\0';
    print("> ");
}

void shell_receive(char character)
{
    if (character == '\n') {
        line[length] = '\0';
        print("\n");
        execute();
        shell_initialize();
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
