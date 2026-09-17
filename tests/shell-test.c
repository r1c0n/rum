#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/terminal.h>

static volatile uint16_t *const screen = (volatile uint16_t *)0xB8000;
static char output[16384];
static size_t output_length;

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
    shell_initialize();
}

static void assert_status(void)
{
    const char *text = "uptime: 7s";
    for (size_t x = 0; text[x]; ++x)
        assert((char)screen[24 * 80 + x] == text[x]);
}

int main(void)
{
    void *mapping = mmap((void *)0xB8000, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapping != MAP_FAILED);
    reset();
    receive("\b\n \t\n");
    assert(strcmp(output, "> \r\n>      \r\n> ") == 0);

    reset();
    receive("  help \t\n");
    assert(strstr(output, "Commands:\r\n"));
    for (const char **name = (const char *[]){"help ", "clear ", "about ", "echo <text>", NULL}; *name; ++name)
        assert(strstr(output, *name));
    receive("about\n");
    assert(strstr(output, "rum OS v0.1.0 (unreleased)\r\n"));
    assert_status();

    reset();
    receive("echo\n");
    assert(strcmp(output, "> echo\r\n\r\n> ") == 0);
    reset();
    receive(" echo   literal %x  'text'  \n");
    assert(strstr(output, "\r\nliteral %x  'text'  \r\n> "));
    receive("echx\bo corrected\n");
    assert(strstr(output, "\r\ncorrected\r\n> "));
    receive("echo\twith tabs\n");
    assert(strstr(output, "\r\nwith tabs\r\n> "));

    reset();
    receive("helpful\nHELP\nhelp x\nabout x\nclear x\n");
    assert(strstr(output, "Unknown command: helpful. Type 'help'.\r\n"));
    assert(strstr(output, "Unknown command: HELP. Type 'help'.\r\n"));
    assert(strstr(output, "Usage: help\r\n"));
    assert(strstr(output, "Usage: about\r\n"));
    assert(strstr(output, "Usage: clear\r\n"));
    assert(!strchr(output, '\x1b'));

    reset();
    receive("echo ");
    for (unsigned i = 0; i < SHELL_LINE_CAPACITY - 6; ++i)
        shell_receive('a');
    size_t full_output = output_length;
    shell_receive('z'); /* Full: no echo and no write beyond the buffer. */
    assert(output_length == full_output);
    receive("\bb\n"); /* Backspace frees one slot at capacity. */
    char expected[SHELL_LINE_CAPACITY];
    memset(expected, 'a', SHELL_LINE_CAPACITY - 7);
    strcpy(expected + SHELL_LINE_CAPACITY - 7, "b\r\n> ");
    assert(strstr(output, expected));
    receive("echo fresh\n");
    assert(strstr(output, "\r\nfresh\r\n> "));
    assert_status();

    reset();
    receive("echo ");
    for (unsigned i = 0; i < 90; ++i)
        shell_receive('a');
    for (unsigned i = 0; i < 100; ++i)
        shell_receive('\b'); /* Across a VGA wrap; can't erase the prompt. */
    assert((char)screen[0] == '>' && (char)screen[1] == ' ');
    for (unsigned x = 2; x < 160; ++x)
        assert((char)screen[x] == ' ');
    receive("help\n");
    assert(strstr(output, "Commands:\r\n"));

    receive("clear\n");
    assert(strstr(output, "\r\n\x1b[2J\x1b[H> "));
    assert((char)screen[0] == '>' && (char)screen[1] == ' ');
    for (unsigned i = 2; i < 24 * 80; ++i)
        assert(screen[i] == 0x0720);
    assert_status();
    receive("echo after clear\n");
    assert(strstr(output, "\r\nafter clear\r\n> "));
    for (unsigned i = 0; i < 32; ++i)
        receive("echo scroll\n");
    assert_status();
    assert(munmap(mapping, 4096) == 0);
    puts("PASS: shell commands, whitespace, usage, unknown commands, editing, line limit, clear/status, scrolling");
    return 0;
}
