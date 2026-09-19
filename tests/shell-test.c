#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <rum/heap.h>
#include <rum/diagnostics.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/shell.h>
#include <rum/terminal.h>
#include <rum/snake.h>
#include <rum/snake_model.h>
#include "page-backend.h"

static volatile uint16_t *const screen = (volatile uint16_t *)0xB8000;
static char output[16384];
static size_t output_length;
static uint32_t ticks;
uint32_t timer_ticks(void) { return ticks; }

/* Hardware capture is exercised by QEMU; host tests use the real formatter
   against a fixed snapshot and the real shell/console. */
bool diagnostics_capture(struct kernel_diagnostics *result)
{
    if (!result) return false;
    *result = (struct kernel_diagnostics){ .cr3 = 0x123000, .esp0 = 0x210000,
        .tasks_ready = true, .tasks = { .current = 7, .count = 1, .stack_pages = 4,
            .tasks = {{ .id = 7, .state = TASK_RUNNING, .stack_base = 0x20C000,
                .stack_top = 0x210000, .directory = 0x123000, .owns_stack = true }} } };
    result->heap = heap_stats();
    result->files = ramfs_stats();
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
    shell_initialize();
}

static void assert_status(void)
{
    const char *text = "uptime: 7s";
    for (size_t x = 0; text[x]; ++x)
        assert((char)screen[24 * 80 + x] == text[x]);
}

static void assert_head(unsigned x, unsigned y)
{
    assert((char)screen[(5 + y) * 80 + 20 + x] == '@');
    assert_status();
}

static void step(enum snake_direction direction, struct snake_model *reference)
{
    char key = direction == SNAKE_UP ? 'w' : direction == SNAKE_RIGHT ? 'd' :
               direction == SNAKE_DOWN ? 's' : 'a';
    shell_receive(key);
    (void)snake_model_turn(reference, direction);
    ticks += SNAKE_STEP_TICKS;
    shell_tick(ticks);
    enum snake_event event = snake_model_step(reference);
    assert(event == SNAKE_MOVED || event == SNAKE_ATE);
    struct snake_cell head = snake_model_cell(reference, 0);
    assert_head(head.x, head.y);
}

static void snake_checks(void)
{
    reset();
    receive("snake x\n");
    assert(strstr(output, "Usage: snake\r\n> ") && !snake_active());
    struct heap_statistics before = heap_stats();
    ticks = 0;
    receive("snake\n");
    assert(snake_active() && strstr(output, "rum_snake_started\r\n"));
    assert(heap_stats().allocations == before.allocations + 1);
    assert_head(20, 8);
    assert(!shell_tick_due(14) && shell_tick_due(15));
    ticks = 15; shell_tick(ticks); assert_head(21, 8);
    receive("P");
    uint16_t paused[24 * 80]; memcpy(paused, (const void *)screen, sizeof paused);
    ticks += 5000; shell_tick(ticks);
    assert(!shell_tick_due(ticks) && !memcmp(paused, (const void *)screen, sizeof paused));
    receive("R"); assert_head(20, 8);
    receive("wa"); ticks += 15; shell_tick(ticks); assert_head(20, 7);
    receive("q");
    assert(!snake_active() && heap_stats().allocations == before.allocations &&
           strstr(output, "rum_snake_quit\r\n> "));
    receive("echo returned\n"); assert(strstr(output, "\r\nreturned\r\n> "));

    /* Unsigned tick subtraction keeps movement correct across PIT wraparound. */
    ticks = UINT32_MAX - 7; receive("snake\n");
    assert(!shell_tick_due(6) && shell_tick_due(7));
    ticks = 7; shell_tick(ticks); assert_head(21, 8);
    receive("q"); assert(!snake_active());

    assert(ramfs_put("snake.score", "bad", 3));
    ticks = 200; receive("snake\n");
    struct snake_model reference;
    snake_model_reset(&reference, ticks ^ 0x72756Du);
    struct snake_cell target = reference.food;
    if (target.y == 8 && target.x < 20) {
        step(SNAKE_UP, &reference);
        while (snake_model_cell(&reference, 0).x != target.x) step(SNAKE_LEFT, &reference);
        step(SNAKE_DOWN, &reference);
    } else {
        while (snake_model_cell(&reference, 0).y != target.y)
            step(target.y < 8 ? SNAKE_UP : SNAKE_DOWN, &reference);
        while (snake_model_cell(&reference, 0).x != target.x)
            step(target.x < 20 ? SNAKE_LEFT : SNAKE_RIGHT, &reference);
    }
    assert(snake_model_score(&reference) == 1);
    receive("q");
    const unsigned char *data; size_t size;
    assert(ramfs_read("snake.score", &data, &size) && size == 2 && !memcmp(data, "1\n", 2));
    assert(strstr(output, "Snake score: 1 | best: 1"));

    /* Exhaust remaining mapped space and disallow new frames to test launch OOM. */
    test_page_budget(0);
    void *held = NULL, *chunk;
    while ((chunk = kmalloc(128))) { *(void **)chunk = held; held = chunk; }
    size_t allocations = heap_stats().allocations;
    receive("snake\n");
    assert(!snake_active() && heap_stats().allocations == allocations &&
           strstr(output, "Cannot start Snake: out of memory."));
    while (held) { void *next = *(void **)held; assert(kfree(held)); held = next; }
    test_page_budget(-1);

    assert(ramfs_remove("snake.score"));
    for (unsigned i = 0; ramfs_stats().files < RAMFS_MAX_FILES; ++i) {
        char name[16]; snprintf(name, sizeof name, "slot%u", i);
        assert(ramfs_put(name, NULL, 0));
    }
    before = heap_stats();
    receive("snake\nq");
    assert(!snake_active() && heap_stats().allocations == before.allocations &&
           ramfs_stats().files == RAMFS_MAX_FILES && !ramfs_read("snake.score", NULL, NULL));
    assert(strstr(output, "RAM file could not be saved."));
    for (unsigned i = 0; i < RAMFS_MAX_FILES - 2; ++i) {
        char name[16]; snprintf(name, sizeof name, "slot%u", i);
        assert(ramfs_remove(name));
    }
    receive("snake\nq");
    assert(ramfs_read("snake.score", &data, &size) && size == 2 && data[0] == '1');
    assert_status();
}

int main(void)
{
    assert(heap_initialize() && ramfs_initialize());
    assert(ramfs_put("welcome.txt", "Welcome to rum.\n", 16));
    assert(ramfs_put("readme.txt", "test seed", 9));
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
    assert(strstr(output, "rum OS v0.2.0\r\n"));
    assert_status();
    reset();
    receive("ls\ncat welcome.txt\nwrite notes.txt hello  from rum\ncat /notes.txt\nmem\n");
    assert(strstr(output, "welcome.txt  ") && strstr(output, "readme.txt  "));
    assert(strstr(output, "Welcome to rum."));
    assert(strstr(output, "\r\nhello  from rum\r\n> "));
    assert(strstr(output, "Heap: ") && strstr(output, "RAM files: 3 files, "));
    receive("write notes.txt replacement\ncat notes.txt\nwrite empty\ncat empty\n");
    assert(strstr(output, "\r\nreplacement\r\n> ") && ramfs_stats().files == 4);
    receive("rm notes.txt\ncat notes.txt\nrm empty\n");
    assert(strstr(output, "File not found: notes.txt\r\n"));
    receive("ls x\ncat\ncat a b\nrm\nrm a b\nmem x\nwrite\nwrite bad/name no\n");
    for (const char **text = (const char *[]){"Usage: ls", "Usage: cat <name>", "Usage: rm <name>",
             "Usage: mem", "Usage: write <name> [text]", "Cannot write file:", NULL}; *text; ++text)
        assert(strstr(output, *text));
    assert_status();

    reset();
    struct heap_statistics diag_before = heap_stats();
    receive("diag x\ndiag\n");
    assert(strstr(output, "Usage: diag\r\n") && strstr(output, "Task: 7 | CR3: 0x00123000"));
    assert(strstr(output, "#7 running stack=0x0020c000..0x00210000 owned"));
    assert(strstr(output, "CR3=0x00123000 borrowed"));
    assert(heap_stats().allocations == diag_before.allocations);
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
    snake_checks();
    assert(munmap(mapping, 4096) == 0);
    puts("PASS: shell/files/editing/clear/status, Snake play/pause/restart/quit, tick wrap, scores, launch/save OOM and allocation cleanup");
    return 0;
}
