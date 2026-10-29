#include <rum/user.h>

#define PAGE 0x80800000u
#define TEST_CLOCK 0xfffffff0u
#define TEST_EVENT 0xfffffff1u
int main(int argc, char **argv);
static struct rum_arguments arguments;
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static uint32_t clock_ticks(void) { return (uint32_t)rum_syscall3(TEST_CLOCK, 0, 0, 0); }
static rum_result_t spawn(const char *mode)
{
    arguments = (struct rum_arguments){ .argc = 2, .string_bytes = 5, .offsets = {0, 5}, .strings = "wait" };
    do { arguments.strings[arguments.string_bytes++] = *mode; } while (*mode++);
    return rum_spawn("/wait.elf", &arguments);
}
static void collect(rum_pid_t child, uint32_t kind, int status)
{
    struct rum_process_result out = { .version = 1, .size = sizeof out };
    check(rum_wait(child, 0, &out) == 1 && out.process_id == child && out.termination == kind &&
          out.status == status && out.fault_vector == (kind == RUM_PROCESS_FAULTED ? 6u : 0u), 100);
}
static rum_result_t raw(struct rum_sleep_request *request)
{
    return rum_syscall3(RUM_SYS_SLEEP, (rum_address_t)(uintptr_t)request, 0, 0);
}
static void normal(void)
{
    struct rum_sleep_request request = { 1, sizeof request, 1, 0, {0, 0} };
    check(!rum_yield() && !rum_sleep(0), 10);
    for (unsigned i = 0; i < 3; ++i)
        check(rum_syscall3(RUM_SYS_YIELD, i == 0, i == 1, i == 2) == -RUM_EINVAL, 11);
    check(raw((void *)UINT32_MAX) == -RUM_EFAULT && raw((void *)(PAGE + 3 * 4096 - 8)) == -RUM_EFAULT &&
          rum_syscall3(RUM_SYS_SLEEP, 0, 1, 0) == -RUM_EINVAL &&
          rum_syscall3(RUM_SYS_SLEEP, 0, 0, 1) == -RUM_EINVAL, 12);
    request.version = 2; check(raw(&request) == -RUM_EINVAL, 13); request.version = 1;
    --request.size; check(raw(&request) == -RUM_EINVAL, 14); ++request.size;
    request.flags = 1; check(raw(&request) == -RUM_EINVAL, 15); request.flags = 0;
    for (unsigned i = 0; i < 2; ++i) {
        request.reserved[i] = 1; check(raw(&request) == -RUM_EINVAL, 16); request.reserved[i] = 0;
    }
    check(rum_sleep(RUM_SLEEP_MAX_MS + 1) == -RUM_ERANGE && rum_sleep(UINT32_MAX) == -RUM_ERANGE, 17);
    struct rum_sleep_request *cross = (void *)(PAGE + 4096 - 8); *cross = request;
    check(!raw(cross) && !raw((void *)(PAGE + 8192)), 18); /* Read-only input packet is valid. */
    const uint32_t durations[] = {1, 10, 11, 25};
    for (unsigned i = 0; i < sizeof durations / sizeof durations[0]; ++i) {
        uint32_t started = clock_ticks();
        check(!rum_sleep(durations[i]) && (uint32_t)(clock_ticks() - started) >= (durations[i] + 9) / 10 + 1, 19);
    }
    rum_result_t first = spawn("yield"), second = spawn("yield");
    check(first > 0 && second > 0 && !rum_yield(), 20);
    collect((rum_pid_t)first, RUM_PROCESS_EXITED, 42); collect((rum_pid_t)second, RUM_PROCESS_EXITED, 42);
    for (unsigned i = 0; i < 16; ++i) {
        rum_result_t child = spawn("long"); check(child > 0 && !rum_sleep(20), 21);
        struct rum_process_result out = { .version = 1, .size = sizeof out };
        check(!rum_wait((rum_pid_t)child, RUM_WAIT_NOHANG, &out) && !rum_terminate((rum_pid_t)child, 0), 22);
        collect((rum_pid_t)child, RUM_PROCESS_CANCELLED, 0);
        child = spawn("short"); check(child > 0, 23); collect((rum_pid_t)child, RUM_PROCESS_EXITED, 42);
    }
    rum_result_t child = spawn("fault"); check(child > 0, 24); collect((rum_pid_t)child, RUM_PROCESS_FAULTED, 0);
}
int main(int argc, char **argv)
{
    check(argc == 2, 1);
    if (equal(argv[1], "normal")) { normal(); return 0; }
    check(rum_open("/data.bin", RUM_OPEN_READ, 0) == 3, 2);
    if (equal(argv[1], "yield")) {
        for (unsigned i = 0; i < 32; ++i) check(!rum_yield() && !rum_sleep(0), 3);
        return 42;
    }
    if (equal(argv[1], "event") || equal(argv[1], "eventcancel") || equal(argv[1], "precancel")) {
        rum_result_t result = rum_syscall3(TEST_EVENT, equal(argv[1], "precancel"), 0, 0);
        check(equal(argv[1], "event") && !result, 4); return 0;
    }
    check(!rum_sleep(equal(argv[1], "long") ? RUM_SLEEP_MAX_MS :
                     equal(argv[1], "latecancel") ? 200 : 1), 5);
    if (equal(argv[1], "fault")) __asm__ volatile ("ud2");
    return 42;
}
