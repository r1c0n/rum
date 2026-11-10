#include <rum/tools.h>
static unsigned char pages[8192] __attribute__((aligned(4096)));
static int same(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void check(int ok, int code) { if (!ok) { tool_print("editing check failed: "); tool_number(code); tool_print("\n"); rum_exit(code); } }
int main(int argc, char **argv)
{
    if (argc > 1 && same(argv[1], "argv")) {
        check(argc == 7 && !*argv[2] && same(argv[3], "a  b") && same(argv[4], "x|y") &&
              same(argv[5], "x'y") && same(argv[6], "x\"y"), 1);
        char text[RUM_COMMAND_TEXT_CAPACITY]; check(rum_command_text(text, sizeof text) == -RUM_EACCES, 2);
        tool_print("parsed argv verified\n"); return 0;
    }
    if (argc == 2 && same(argv[1], "notice")) {
        check(!rum_sleep(600), 3); tool_print("asynchronous output\n"); return 0;
    }
    if (argc == 2 && same(argv[1], "inline")) {
        check(rum_terminal_line("x", 1, 0) == -RUM_EACCES, 4);
        check(rum_terminal_scroll(RUM_SCROLL_UP) == -RUM_EACCES, 15);
        check(!rum_terminal_begin() && rum_terminal_line("x", 1, 0) == -RUM_EACCES &&
              rum_terminal_scroll(RUM_SCROLL_UP) == -RUM_EACCES && !rum_terminal_end(), 5);
        struct rum_terminal_request invalid = {.version = 1, .size = sizeof invalid, .operation = RUM_TERM_BEGIN, .flags = 2};
        check(rum_syscall3(RUM_SYS_TERMINAL, (rum_address_t)(uintptr_t)&invalid, 0, 0) == -RUM_EINVAL, 6);
        check(!rum_terminal_begin_inline(), 7);
        check(rum_terminal_scroll(UINT32_MAX) == -RUM_EINVAL, 16);
        invalid = (struct rum_terminal_request){.version = 1, .size = sizeof invalid, .operation = RUM_TERM_SCROLL,
            .x = RUM_SCROLL_UP, .count = 1};
        check(rum_syscall3(RUM_SYS_TERMINAL, (rum_address_t)(uintptr_t)&invalid, 0, 0) == -RUM_EINVAL, 17);
        struct rum_terminal_capabilities before, after;
        check(!rum_terminal_query(&before) && before.mode == (RUM_TERM_ACTIVE | RUM_TERM_OWNED | RUM_TERM_INLINE_MODE), 8);
        char *line = (char *)pages + 4096 - 2;
        line[0] = '>'; line[1] = ' '; line[2] = 'x';
        check(!rum_terminal_line(line, 3, 2) && !rum_terminal_query(&before), 9);
        check(rum_terminal_line("bad\nline", 8, 0) == -RUM_EINVAL &&
              rum_terminal_line("x", RUM_TERM_MAX_LINE + 1, 0) == -RUM_EINVAL &&
              rum_terminal_line("x", 1, UINT32_MAX) == -RUM_EINVAL &&
              rum_terminal_line((void *)UINT32_MAX, 1, 0) == -RUM_EFAULT &&
              !rum_terminal_query(&after) && before.cursor_x == after.cursor_x && before.cursor_y == after.cursor_y, 10);
        struct rum_terminal_cell *cells = (void *)(pages + 4096 - 4);
        cells[0] = (struct rum_terminal_cell){'>', RUM_COLOR_LIGHT_GREEN, RUM_COLOR_BLACK, 0};
        cells[1] = (struct rum_terminal_cell){'x', RUM_COLOR_WHITE, RUM_COLOR_BLACK, 0};
        check(!rum_terminal_styled_line(cells, 2, 1) && !rum_terminal_query(&before), 30);
        cells[1].foreground = 16;
        check(rum_terminal_styled_line(cells, 2, 1) == -RUM_EINVAL &&
              rum_terminal_styled_line(cells, UINT32_MAX, 0) == -RUM_EINVAL &&
              rum_terminal_styled_line((void *)UINT32_MAX, 1, 0) == -RUM_EFAULT &&
              !rum_terminal_query(&after) && after.cursor_x == before.cursor_x && after.cursor_y == before.cursor_y, 31);
        check(!rum_terminal_line((void *)UINT32_MAX, 0, 0) && !rum_terminal_end(), 11);
        tool_print("inline ABI verified\n"); return 0;
    }
    if (argc == 2 && same(argv[1], "scroll")) {
        for (unsigned i = 0; i < 30; ++i) tool_print("scroll fixture\n");
        check(!rum_terminal_begin_inline() && !rum_terminal_line("> draft", 7, 3), 18);
        struct rum_terminal_capabilities before, after;
        check(!rum_terminal_query(&before) && (before.features & RUM_TERM_FEATURE_SCROLLBACK), 19);
        check(!rum_terminal_scroll(RUM_SCROLL_UP) && !rum_terminal_query(&after) &&
              (after.mode & RUM_TERM_SCROLLED) && !after.cursor_visible &&
              after.cursor_x == before.cursor_x && after.cursor_y == before.cursor_y, 20);
        check(!rum_terminal_scroll(RUM_SCROLL_DOWN) && !rum_terminal_query(&after) &&
              !(after.mode & RUM_TERM_SCROLLED) && after.cursor_visible, 21);
        check(!rum_terminal_scroll(RUM_SCROLL_UP) && !rum_terminal_scroll(RUM_SCROLL_LIVE) &&
              !rum_terminal_query(&after) && !(after.mode & RUM_TERM_SCROLLED), 22);
        check(!rum_terminal_scroll(RUM_SCROLL_UP) && !rum_terminal_end() &&
              !rum_terminal_query(&after) && !after.mode && after.cursor_visible, 23);
        tool_print("\nscroll ABI verified\n"); return 0;
    }
    if (argc == 2 && same(argv[1], "keys")) {
        static const uint32_t expected[] = { RUM_KEY_LEFT, RUM_KEY_RIGHT, RUM_KEY_HOME, RUM_KEY_END, RUM_KEY_DELETE,
            RUM_KEY_UP, RUM_KEY_DOWN, RUM_KEY_PAGEUP, RUM_KEY_PAGEDOWN };
        check(!rum_terminal_begin_inline(), 12); tool_print("navigation ready\n");
        for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
            struct rum_terminal_event event;
            check(rum_terminal_read(&event, 1, 10000, 0) == 1 && event.type == RUM_TERM_KEY_EVENT && event.value == expected[i] && !event.reserved, 13);
        }
        check(!rum_terminal_end(), 14); tool_print("navigation ABI verified\n"); return 0;
    }
    return 1;
}
