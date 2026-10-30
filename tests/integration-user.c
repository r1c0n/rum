#include <rum/user.h>

#define REPORT 0xffffffd0u
#define PROGRESS 0xffffffd1u
int main(int argc, char **argv);
static struct rum_arguments arguments;
static volatile uint32_t private_page[1024] __attribute__((aligned(4096)));
static unsigned char bytes[4097];
static void check(int ok, int code) { if (!ok) rum_exit(code); }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static void say(const char *s)
{
    unsigned n = 0; while (s[n]) ++n; check(rum_write(1, s, n) == (int)n, 90);
}
static void args(const char *mode, unsigned seed)
{
    arguments = (struct rum_arguments){ .argc = 3, .string_bytes = 12, .offsets = {0, 12}, .strings = "/stress.elf" };
    do { arguments.strings[arguments.string_bytes++] = *mode; } while (*mode++);
    arguments.offsets[2] = arguments.string_bytes;
    arguments.strings[arguments.string_bytes++] = (char)('0' + seed);
    arguments.strings[arguments.string_bytes++] = 0;
}
static rum_pid_t spawn(const char *mode, unsigned seed)
{
    args(mode, seed); rum_result_t pid = rum_spawn("/stress.elf", &arguments); check(pid > 0, 91); return (rum_pid_t)pid;
}
static struct rum_process_result result(void)
{
    return (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION, .size = sizeof(struct rum_process_result) };
}
static void collect(rum_pid_t pid, unsigned termination, int status)
{
    struct rum_process_result r = result();
    check(rum_wait(pid, 0, &r) == 1 && r.process_id == pid && r.termination == termination, 92);
    if (r.status != status) rum_exit(r.status ? r.status : 93);
}
static unsigned char pattern(unsigned i, unsigned seed, unsigned iteration)
{
    return (unsigned char)((i * 37u) ^ (i >> 7) ^ (seed * 23u) ^ (iteration * 11u));
}
static void file(unsigned seed, unsigned iteration, int hold)
{
    const char *name = seed == 1 ? "IOA.BIN" : seed == 2 ? "IOB.BIN" : seed == 3 ? "HOLD.BIN" : "FAULT.BIN";
    for (unsigned i = 0; i < sizeof bytes; ++i) bytes[i] = pattern(i, seed, iteration);
    check(rum_replace(name, bytes, sizeof bytes) == 0, 10);
    rum_result_t handle = rum_open(name, RUM_OPEN_READ | RUM_OPEN_WRITE, 0); check(handle == 3, 11);
    /* Keep a real FAT handle and cwd pinned across a scheduling wait. */
    check(rum_sleep(1) == 0, 12);
    unsigned offset = 0;
    while (offset < sizeof bytes) {
        rum_result_t n = rum_read(handle, bytes, sizeof bytes - offset);
        check(n > 0 && n <= 512, 13);
        for (unsigned i = 0; i < (unsigned)n; ++i) check(bytes[i] == pattern(offset + i, seed, iteration), 14);
        offset += (unsigned)n;
    }
    check(rum_read(handle, bytes, 1) == 0, 15);
    uint64_t position;
    check(rum_seek(handle, -3, RUM_SEEK_END, &position) == 0 && position == sizeof bytes - 3 &&
          rum_read(handle, bytes, 3) == 3, 16);
    for (unsigned i = 0; i < 3; ++i) check(bytes[i] == pattern(sizeof bytes - 3 + i, seed, iteration), 17);
    check(rum_flush(handle) == 0, 18);
    if (hold) {
        check(rum_replace("/ready.flag", "1", 1) == 0, 19);
        if (hold == 2) __asm__ volatile("ud2");
        check(rum_sleep(RUM_SLEEP_MAX_MS) == 0, 20); rum_exit(21);
    }
    check(rum_close(handle) == 0, 22);
}
int main(int argc, char **argv)
{
    check(argc == 3 && rum_chdir("/disk/STRESS") == 0, 1);
    unsigned seed = (unsigned)(argv[2][0] - '0'); const char *mode = argv[1];
    if (equal(mode, "cpu")) {
        check(seed == 1 || seed == 2, 2);
        for (unsigned i = 1; i < 1024; ++i) private_page[i] = (seed * 0x112233u) ^ i;
        for (;;) {
            for (unsigned i = 0; i < 50000; ++i) ++private_page[0];
            for (unsigned i = 1; i < 1024; ++i) check(private_page[i] == ((seed * 0x112233u) ^ i), 3);
            check(rum_syscall3(REPORT, seed, (rum_address_t)(uintptr_t)private_page, 0) == 0, 4);
        }
    }
    if (equal(mode, "io")) { for (unsigned i = 0; i < 8; ++i) file(seed, i, 0); return 0; }
    if (equal(mode, "hold")) { file(3, 0, 1); return 23; }
    if (equal(mode, "fault")) { file(4, 0, 2); return 24; }
    if (equal(mode, "timer")) { check(rum_sleep(150) == 0, 5); return 0; }
    if (equal(mode, "input")) {
        say("rum_integration_input_ready\n");
        for (unsigned i = 0; i < 7; ++i) {
            char c; check(rum_read(0, &c, 1) == 1 && c == "island\n"[i], 6);
        }
        return 0;
    }
    check(rum_replace("/ready.flag", "0", 1) == 0, 7);
    rum_pid_t a = spawn("cpu", 1), b = spawn("cpu", 2);
    if (equal(mode, "normal")) {
        rum_pid_t ioa = spawn("io", 1), iob = spawn("io", 2), timer = spawn("timer", 0);
        args("input", 0); struct rum_process_result r = result();
        check(rum_run("/stress.elf", &arguments, &r) == 0 && r.termination == RUM_PROCESS_EXITED && !r.status, 8);
        collect(ioa, RUM_PROCESS_EXITED, 0); collect(iob, RUM_PROCESS_EXITED, 0); collect(timer, RUM_PROCESS_EXITED, 0);
        rum_pid_t fault = spawn("fault", 4); collect(fault, RUM_PROCESS_FAULTED, 0);
        for (unsigned tries = 0; rum_syscall3(PROGRESS, 0, 0, 0) != 2; ++tries)
            check(tries < 100 && rum_sleep(10) == 0, 9);
        check(rum_terminate(a, 0) == 0 && rum_terminate(b, 0) == 0, 25);
        collect(a, RUM_PROCESS_CANCELLED, 0); collect(b, RUM_PROCESS_CANCELLED, 0); return 0;
    }
    (void)spawn("hold", 3);
    for (unsigned tries = 0;; ++tries) {
        rum_result_t fd = rum_open("/ready.flag", RUM_OPEN_READ, 0); char ready;
        check(fd == 3 && rum_read(fd, &ready, 1) == 1 && rum_close(fd) == 0, 26);
        if (ready == '1') break;
        check(tries < 100 && rum_sleep(10) == 0, 27);
    }
    if (equal(mode, "exit")) return 17;
    if (equal(mode, "parent-fault")) { __asm__ volatile("ud2"); return 28; }
    check(equal(mode, "cancel"), 29);
    say("rum_integration_cancel_ready\n");
    check(rum_sleep(RUM_SLEEP_MAX_MS) == 0, 30); return 31;
}
