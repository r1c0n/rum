#include <rum/tools.h>

static unsigned char pages[3 * 4096] __attribute__((aligned(4096)));
static int equal(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void check(int ok, int code)
{
    if (!ok) { tool_print("terminal check "); tool_number(code); tool_print(" failed\n"); rum_exit(code); }
}
static rum_result_t raw(struct rum_terminal_request *r)
{
    return rum_syscall3(RUM_SYS_TERMINAL, (rum_address_t)(uintptr_t)r, 0, 0);
}
static void defaults(void)
{
    struct rum_terminal_capabilities c;
    check(!rum_terminal_query(&c) && c.width == 80 && c.height == 24 && c.version == 1 && c.size == sizeof c &&
          !c.mode && c.cursor_visible == 1 && c.foreground == RUM_COLOR_LIGHT_GREY && c.background == 0, 10);
}
static void invalid(void)
{
    struct rum_terminal_capabilities c;
    struct rum_terminal_request r = { .version = 1, .size = sizeof r, .operation = RUM_TERM_QUERY,
        .data = (rum_address_t)(uintptr_t)&c };
    check(rum_syscall3(RUM_SYS_TERMINAL, UINT32_MAX, 0, 0) == -RUM_EFAULT &&
          rum_syscall3(RUM_SYS_TERMINAL, (rum_address_t)(uintptr_t)&r, 1, 0) == -RUM_EINVAL, 11);
    r.version = 2; check(raw(&r) == -RUM_EINVAL, 12); r.version = 1;
    r.size--; check(raw(&r) == -RUM_EINVAL, 13); r.size++;
    r.reserved[1] = 1; check(raw(&r) == -RUM_EINVAL, 14); r.reserved[1] = 0;
    r.now_ticks = 1; check(raw(&r) == -RUM_EINVAL, 15); r.now_ticks = 0;
    r.flags = 1; check(raw(&r) == -RUM_EINVAL, 16); r.flags = 0;
    r.operation = 99; check(raw(&r) == -RUM_EINVAL, 17); r.operation = RUM_TERM_QUERY;
    r.data = (rum_address_t)(uintptr_t)"readonly"; check(raw(&r) == -RUM_EFAULT, 18);
    r.data = (rum_address_t)(uintptr_t)&r; check(raw(&r) == -RUM_EFAULT, 19);
    r.data = (rum_address_t)(uintptr_t)(pages + 4096 - 20); check(!raw(&r), 20);
    check(rum_console(RUM_CONSOLE_SNAKE) == -RUM_ENOSYS, 21);
    check(!rum_terminal_begin() && rum_terminal_begin() == -RUM_EBUSY, 22);
    struct rum_terminal_cell cells[2] = {{'T', 10, 0, 0}, {'X', 7, 0, 0}};
    check(rum_terminal_cells(UINT32_MAX, 0, cells, 2) == -RUM_EINVAL &&
          rum_terminal_cells(79, 0, cells, 2) == -RUM_EINVAL &&
          rum_terminal_cells(0, 24, cells, 1) == -RUM_EINVAL &&
          rum_terminal_cells(0, 0, cells, UINT32_MAX) == -RUM_EINVAL, 23);
    check(rum_terminal_cells(0, 0, (void *)UINT32_MAX, 2) == -RUM_EFAULT &&
          rum_terminal_cells(0, 0, (void *)UINT32_MAX, 0) == 0, 24);
    cells[1].reserved = 1; check(rum_terminal_cells(0, 0, cells, 2) == -RUM_EINVAL, 25); cells[1].reserved = 0;
    cells[1].character = 27; check(rum_terminal_cells(0, 0, cells, 2) == -RUM_EINVAL, 26); cells[1].character = 'X';
    cells[1].foreground = 16; check(rum_terminal_cells(0, 0, cells, 2) == -RUM_EINVAL, 27); cells[1].foreground = 7;
    check(rum_terminal_cursor(80, 0, 0) == -RUM_EINVAL && rum_terminal_cursor(0, 24, 1) == -RUM_EINVAL &&
          rum_terminal_cursor(0, 0, 2) == -RUM_EINVAL && rum_terminal_color(16, 0) == -RUM_EINVAL, 28);
    struct rum_terminal_event events[2]; uint32_t now;
    check(rum_terminal_read(events, 0, 0, &now) == -RUM_EINVAL &&
          rum_terminal_read(events, UINT32_MAX, 0, &now) == -RUM_EINVAL &&
          rum_terminal_read(events, 1, RUM_TERM_MAX_WAIT_MS + 1, &now) == -RUM_ERANGE &&
          rum_terminal_read((void *)UINT32_MAX, 1, 0, &now) == -RUM_EFAULT, 29);
    for (unsigned i = 0; i < sizeof events; ++i) ((unsigned char *)events)[i] = 0xA5;
    check(rum_terminal_read(events, 2, 0, &now) == 0, 30);
    uint32_t before = now;
    check(rum_terminal_read(events, 2, 20, &now) == 0 && now - before >= 2 && now - before < 1000, 31);
    for (unsigned i = 0; i < sizeof events; ++i) check(((unsigned char *)events)[i] == 0xA5, 32);
    struct rum_terminal_cell *cross = (void *)(pages + 4096 - 2); *cross = cells[0];
    check(!rum_terminal_cells(0, 0, cross, 1) && !rum_terminal_end(), 33);
}
int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "defaults";
    if (equal(mode, "defaults")) { defaults(); tool_print("terminal defaults restored\n"); return 0; }
    if (equal(mode, "validate")) { defaults(); invalid(); defaults(); tool_print("terminal ABI validated\n"); return 0; }
    if (equal(mode, "denied")) {
        struct rum_terminal_capabilities caps; struct rum_terminal_cell cell = {'X', 7, 0, 0};
        struct rum_terminal_event event;
        check(!rum_terminal_query(&caps) && !(caps.mode & RUM_TERM_OWNED) && rum_terminal_begin() == -RUM_EACCES &&
              rum_terminal_cells(0, 0, &cell, 1) == -RUM_EACCES && rum_terminal_color(1, 0) == -RUM_EACCES &&
              rum_terminal_cursor(0, 0, 0) == -RUM_EACCES && rum_terminal_read(&event, 1, 0, 0) == -RUM_EACCES, 40);
        tool_print("background terminal denied\n"); return 0;
    }
    check(!rum_terminal_begin() && !rum_terminal_color(RUM_COLOR_YELLOW, RUM_COLOR_BLUE) && !rum_terminal_cursor(5, 6, 0), 41);
    if (equal(mode, "nested")) {
        struct rum_arguments args = {0}; char *words[] = {"term", "defaults"};
        struct rum_process_result child = { .version = 1, .size = sizeof child };
        check(!tool_arguments(&args, 2, words) && rum_run("/MISSING.ELF", &args, &child) == -RUM_ENOENT, 42);
        struct rum_terminal_capabilities caps;
        check(!rum_terminal_query(&caps) && (caps.mode & RUM_TERM_OWNED), 43);
        check(!rum_run("/RUM/TERM.ELF", &args, &child) && child.status == 0, 44);
        defaults(); check(rum_terminal_end() == -RUM_EACCES && !rum_terminal_begin() && !rum_terminal_end(), 45);
        tool_print("nested terminal restored\n"); return 0;
    }
    if (equal(mode, "exit")) return 0;
    if (equal(mode, "fault")) { __asm__ volatile("ud2"); return 1; }
    if (equal(mode, "partial")) { check(rum_terminal_color(16, 0) == -RUM_EINVAL, 46); return 1; }
    if (equal(mode, "readsafe")) {
        tool_print("terminal input ready\n");
        struct rum_terminal_event first;
        check(rum_terminal_read(&first, 1, RUM_TERM_MAX_WAIT_MS, 0) == 1 && first.value == 'a', 47);
        (void)rum_sleep(100);
        check(rum_terminal_read((void *)(UINT32_MAX - 16), 2, 0, 0) == -RUM_EFAULT, 48);
        struct rum_terminal_request *cross = (void *)(pages + 4096 - 16);
        struct rum_terminal_event *event = (void *)(pages + 8192 - 8);
        for (unsigned i = 0; i < 32; ++i) ((unsigned char *)event)[i] = 0xA5;
        *cross = (struct rum_terminal_request){ .version = 1, .size = sizeof *cross, .operation = RUM_TERM_READ,
            .count = 1, .timeout_ms = 1000, .data = (rum_address_t)(uintptr_t)event };
        check(raw(cross) == 1 && event->type == RUM_TERM_TEXT_EVENT && event->value == 'z' && !event->reserved, 49);
        for (unsigned i = 16; i < 32; ++i) check(((unsigned char *)event)[i] == 0xA5, 50);
        check(!rum_terminal_end(), 51); tool_print("terminal input validated\n"); return 0;
    }
    if (equal(mode, "display")) {
        const char label[] = "TERMINAL"; struct rum_terminal_cell row[8];
        for (unsigned i = 0; i < 8; ++i) row[i] = (struct rum_terminal_cell){(uint8_t)label[i], 10, 0, 0};
        check(!rum_terminal_cells(0, 0, row, 8), 52);
        row[0].character = 'Z'; row[1].reserved = 1;
        check(rum_terminal_cells(0, 0, row, 2) == -RUM_EINVAL, 53);
        struct rum_terminal_cell corner = {'@', 14, 1, 0}; check(!rum_terminal_cells(79, 23, &corner, 1), 54);
        struct rum_arguments args = {0}; char *words[] = {"term", "denied"};
        struct rum_process_result child = { .version = 1, .size = sizeof child };
        check(!tool_arguments(&args, 2, words), 59);
        rum_result_t pid = rum_spawn("/RUM/TERM.ELF", &args);
        check(pid > 0 && rum_wait((rum_pid_t)pid, 0, &child) == 1 && child.status == 0, 60);
        struct rum_terminal_capabilities caps;
        check(!rum_terminal_query(&caps) && caps.cursor_x == 5 && caps.cursor_y == 6 &&
              caps.foreground == 14 && caps.background == 1 && !caps.cursor_visible, 55);
        tool_print("terminal display ready\n"); check(!rum_terminal_cursor(5, 6, 0), 56);
    } else tool_print("terminal cancellation ready\n");
    struct rum_terminal_event event;
    for (;;) {
        check(rum_terminal_read(&event, 1, RUM_TERM_MAX_WAIT_MS, 0) == 1, 57);
        if (event.value == 'q') break;
    }
    check(!rum_terminal_end(), 58); return 0;
}
