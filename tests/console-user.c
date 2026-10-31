#include <rum/user.h>

#define TEST_ALONE 0xffffffe0u
#define TEST_CONSTRUCT 0xffffffe1u
#define TEST_IO 0xffffffe2u
int main(int argc, char **argv);
static const char *program;
static struct rum_arguments arguments;
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static unsigned length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void say(const char *s) { unsigned n = length(s); check(rum_write(1, s, n) == (int)n, 90); }
static void args(const char *mode, const char *extra)
{
    arguments = (struct rum_arguments){0};
    const char *values[] = {program, mode, extra};
    for (unsigned i = 0; i < 3; ++i) {
        arguments.offsets[arguments.argc++] = arguments.string_bytes;
        const char *p = values[i];
        do { arguments.strings[arguments.string_bytes++] = *p; } while (*p++);
    }
}
static struct rum_process_result result(void)
{
    return (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION, .size = sizeof(struct rum_process_result) };
}
static void run(const char *mode, unsigned termination, int status)
{
    args(mode, ""); struct rum_process_result r = result();
    rum_result_t error = rum_run(program, &arguments, &r);
    if (error || r.termination != termination || r.status != status) {
        say("unexpected child result: "); say(mode); say("\n");
        if (r.status) rum_exit(r.status);
        rum_exit(error ? 91 : 99);
    }
    if (termination == RUM_PROCESS_FAULTED) check(r.fault_vector == 6, 92);
}
static rum_pid_t spawn(const char *mode, const char *extra)
{
    args(mode, extra); rum_result_t pid = rum_spawn(program, &arguments); check(pid > 0, 93); return (rum_pid_t)pid;
}
static void collect(rum_pid_t pid)
{
    struct rum_process_result r = result();
    check(rum_wait(pid, 0, &r) == 1 && r.process_id == pid && r.termination == RUM_PROCESS_EXITED, 94);
    if (r.status) rum_exit(r.status);
}
static char input(void) { char c; check(rum_read(0, &c, 1) == 1, 95); return c; }
static void denied(void)
{
    char c;
    check(rum_read(0, &c, 1) == -RUM_EACCES && rum_console(RUM_CONSOLE_CLEAR) == -RUM_EACCES &&
          rum_session(RUM_SESSION_CHDIR, "/") == -RUM_EACCES &&
          rum_session(RUM_SESSION_EXIT, 0) == -RUM_EACCES &&
          rum_session(RUM_SESSION_RECOVERY, 0) == -RUM_EACCES, 96);
    args("exit", ""); struct rum_process_result r = result();
    check(rum_run(program, &arguments, &r) == -RUM_EBUSY &&
          rum_run_command(program, &arguments, "", &r) == -RUM_EACCES, 97);
}
static void alone(void) { check(rum_syscall3(TEST_ALONE, 0, 0, 0) == 1, 98); }
/* The normal kernel boots this same ELF as its initial shell for supervision
   tests. RAM handshake files survive each restart; no test syscall is used. */
static int supervised(void)
{
    char turn = '0', ready = 0;
    rum_result_t fd = rum_open("/session.TRN", RUM_OPEN_READ, 0);
    if (fd >= 0) { check(rum_read(fd, &turn, 1) == 1 && rum_close(fd) == 0, 70); }
    ++turn; check(turn >= '1' && turn <= '3' && rum_replace("/session.TRN", &turn, 1) == 0, 71);
    char extra[] = {turn, 0}; (void)spawn("orphan", extra);
    for (unsigned tries = 0; tries < 300 && ready != turn; ++tries) {
        fd = rum_open("/session.RDY", RUM_OPEN_READ, 0);
        if (fd >= 0) { check(rum_read(fd, &ready, 1) == 1 && rum_close(fd) == 0, 72); }
        if (ready != turn) check(rum_sleep(1) == 0, 73);
    }
    check(ready == turn, 74);
    if (turn == '2') __asm__ volatile("ud2");
    return turn == '1' ? 77 : 0;
}
int main(int argc, char **argv)
{
    program = argv[0];
    if (argc == 1) return supervised();
    check(argc == 3, 1); const char *mode = argv[1];
    if (equal(mode, "orphan")) {
        denied();
        check(rum_open("/readme.txt", RUM_OPEN_READ, 0) == 3 &&
              rum_replace("/session.RDY", argv[2], 1) == 0, 2);
        say("rum_console_orphan_ready\n");
        check(rum_sleep(RUM_SLEEP_MAX_MS) == 0, 3); return 4;
    }
    if (equal(mode, "probe")) {
        for (unsigned i = 0; i < 32; ++i) { denied(); check(rum_sleep(1) == 0, 5); } return 0;
    }
    if (equal(mode, "spin")) { for (;;) __asm__ volatile("pause"); }
    if (equal(mode, "grandchild")) { say("rum_console_nested_ready\n"); for (;;) __asm__ volatile("pause"); }
    if (equal(mode, "middle")) {
        run("grandchild", RUM_PROCESS_CANCELLED, 0);
        say("rum_console_parent_resumed\n"); check(input() == 'p', 6); return 17;
    }
    if (equal(mode, "fault-input")) {
        say("rum_console_fault_input_ready\n"); check(input() == 'x', 7); __asm__ volatile("ud2"); return 8;
    }
    if (equal(mode, "writer")) {
        char text[64];
        for (unsigned i = 0; i < 8; ++i) text[i] = "chunk-a:"[i];
        text[6] = argv[2][0];
        for (unsigned i = 8; i < 63; ++i) text[i] = argv[2][0] == 'a' ? 'A' : 'B';
        text[63] = '\n';
        for (unsigned i = 0; i < 16; ++i) {
            check(rum_write(1, text, sizeof text) == sizeof text && rum_yield() == 0, 9);
        }
        return 0;
    }
    if (equal(mode, "construct")) {
        check(rum_chdir("/DOCS") == 0 && rum_syscall3(TEST_CONSTRUCT, 0, 0, 0) == 0, 10);
        run("exit", RUM_PROCESS_EXITED, 0); return 11; /* Cancellation must prevent publication. */
    }
    if (equal(mode, "disk-read")) {
        char data[8]; rum_result_t fd = rum_open("/DOCS/README.TXT", RUM_OPEN_READ, 0);
        check(fd == 3 && rum_syscall3(TEST_IO, 0, 0, 0) == 0, 12);
        (void)rum_read(fd, data, sizeof data); return 13; /* Cancel before returning to user mode. */
    }
    if (equal(mode, "exit")) return 0;
    check(equal(mode, "main"), 14); alone();
    rum_pid_t background = spawn("probe", "");
    say("rum_console_compete_ready\n"); collect(background); check(input() == 'x', 15); alone();
    say("rum_console_shell_control_ready\n"); check(input() == 3, 16);
    run("fault-input", RUM_PROCESS_FAULTED, 0); check(input() == 'y', 17); alone();
    background = spawn("spin", ""); run("middle", RUM_PROCESS_EXITED, 17);
    struct rum_process_result r = result();
    check(rum_wait(background, RUM_WAIT_NOHANG, &r) == 0 && rum_terminate(background, 0) == 0 &&
          rum_wait(background, 0, &r) == 1 && r.termination == RUM_PROCESS_CANCELLED, 18); alone();
    args("exit", ""); r = result();
    check(rum_run("/absent.elf", &arguments, &r) == -RUM_ENOENT &&
          rum_run("/data.bin", &arguments, &r) == -RUM_ENOEXEC, 19); alone();
    say("rum_console_load_failure_ready\n"); check(input() == 'f', 20);
    rum_pid_t a = spawn("writer", "a"), b = spawn("writer", "b"); collect(a); collect(b); alone();
    run("construct", RUM_PROCESS_CANCELLED, 0); alone();
    run("disk-read", RUM_PROCESS_CANCELLED, 0); alone();
    return 0;
}
