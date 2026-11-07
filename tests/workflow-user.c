#include <rum/tools.h>

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
int main(int argc, char **argv)
{
    if (argc == 4 && equal(argv[1], "append")) {
        rum_result_t fd = rum_open(argv[2], RUM_OPEN_WRITE, RUM_OPEN_CREATE | RUM_OPEN_APPEND);
        if (fd < 0) return 10;
        for (unsigned i = 0; i < 32; ++i) {
            unsigned char record[32];
            for (unsigned j = 0; j < sizeof record; ++j) record[j] = (unsigned char)(argv[3][0] + j);
            record[1] = (unsigned char)i;
            if (rum_write((rum_handle_t)fd, record, sizeof record) != sizeof record) return 11;
            (void)rum_yield();
        }
        return rum_close((rum_handle_t)fd) ? 12 : 0;
    }
    if (argc == 2 && equal(argv[1], "input")) {
        const char ready[] = "input ready\n";
        if (rum_write(RUM_STDERR, ready, sizeof ready - 1) != sizeof ready - 1) return 20;
        char c;
        if (rum_isatty(RUM_STDIN) != 1 || rum_isatty(RUM_STDOUT) != 0 || rum_isatty(32) != -RUM_EBADF) return 21;
        if (rum_read(RUM_STDIN, &c, 1) != 1 || rum_write(RUM_STDOUT, &c, 1) != 1) return 22;
        return 0;
    }
    if (argc == 2 && equal(argv[1], "stderr")) {
        const char err[] = "stderr stays on console\n";
        if (rum_write(RUM_STDERR, err, sizeof err - 1) != sizeof err - 1) return 30;
        tool_print("output bytes\n"); return 0;
    }
    if (argc == 2 && equal(argv[1], "authority")) {
        struct rum_arguments arguments = {0};
        char *words[] = {"wflow", "stderr"};
        if (tool_arguments(&arguments, 2, words)) return 40;
        if (rum_session(RUM_SESSION_PIPELINE_BEGIN, 0) != -RUM_EACCES ||
            rum_session(RUM_SESSION_PIPELINE_END, 0) != -RUM_EACCES ||
            rum_spawn_with_handles("/RUM/WFLOW.ELF", &arguments, 0, 0, RUM_SPAWN_PIPELINE) != -RUM_EACCES ||
            rum_syscall3(RUM_SYS_ISATTY, 1, 1, 0) != -RUM_EINVAL) return 41;
        tool_print("pipeline authority preserved\n"); return 0;
    }
    if (argc == 2 && equal(argv[1], "invalid-open")) {
        if (rum_open("/INVALID.TXT", RUM_OPEN_READ, RUM_OPEN_CREATE) != -RUM_EINVAL ||
            rum_open("/INVALID.TXT", RUM_OPEN_WRITE, 16) != -RUM_EINVAL ||
            rum_open("/INVALID.TXT", RUM_OPEN_READ, RUM_OPEN_DIRECTORY | RUM_OPEN_APPEND) != -RUM_EINVAL)
            return 50;
        tool_print("open flags validated\n"); return 0;
    }
    return 1;
}
