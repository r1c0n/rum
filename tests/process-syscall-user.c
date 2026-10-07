#include <rum/user.h>

#define PAGE 0x80800000u
int main(int argc, char **argv);
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static struct rum_arguments args(const char *mode)
{
    struct rum_arguments p = { .argc = 3, .string_bytes = 11, .offsets = {0, 11}, .strings = "/probe.elf" };
    while (*mode) p.strings[p.string_bytes++] = *mode++;
    p.strings[p.string_bytes++] = 0;
    p.offsets[2] = p.string_bytes;
    p.strings[p.string_bytes++] = 0; /* Empty arguments are valid. */
    return p;
}
static struct rum_process_result result(void)
{
    return (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION, .size = sizeof(struct rum_process_result) };
}
static rum_result_t raw(struct rum_run_request *p)
{
    return rum_syscall3(RUM_SYS_RUN, (rum_address_t)(uintptr_t)p, 0, 0);
}
static void invalid(void)
{
    struct rum_arguments a = args("child");
    struct rum_process_result r = result();
    struct rum_run_request p = {1, sizeof p, (rum_address_t)(uintptr_t)"/probe.elf",
        (rum_address_t)(uintptr_t)&a, (rum_address_t)(uintptr_t)&r, 0, {0, 0}};
    check(rum_syscall3(RUM_SYS_RUN, UINT32_MAX, 0, 0) == -RUM_EFAULT, 10);
    p.version = 2; check(raw(&p) == -RUM_EINVAL, 11); p.version = 1;
    --p.size; check(raw(&p) == -RUM_EINVAL, 12); ++p.size;
    p.flags = 1; check(raw(&p) == -RUM_EINVAL, 13); p.flags = 0;
    for (unsigned i = 0; i < 2; ++i) { p.reserved[i] = 1; check(raw(&p) == -RUM_EINVAL, 14); p.reserved[i] = 0; }
    r.version = 2; check(raw(&p) == -RUM_EINVAL, 15); r = result();
    --r.size; check(raw(&p) == -RUM_EINVAL, 16); r = result();
    r.reserved = 1; check(raw(&p) == -RUM_EINVAL, 17); r = result();
    p.result = PAGE + 2 * 4096; check(raw(&p) == -RUM_EFAULT, 18);
    p.result = PAGE + 2 * 4096 - 8; check(raw(&p) == -RUM_EFAULT, 19);
    p.result = (rum_address_t)(uintptr_t)&r;
    p.arguments = PAGE + 3 * 4096 - 128; check(raw(&p) == -RUM_EFAULT, 20);
    p.arguments = (rum_address_t)(uintptr_t)&a;
    a.argc = 0; check(raw(&p) == -RUM_EINVAL, 21); a = args("child");
    a.argc = 33; check(raw(&p) == -RUM_EINVAL, 22); a = args("child");
    a.string_bytes = 4097; check(raw(&p) == -RUM_EINVAL, 23); a = args("child");
    a.offsets[1]++; check(raw(&p) == -RUM_EINVAL, 24); a = args("child");
    a.strings[a.string_bytes - 1] = 'x'; check(raw(&p) == -RUM_EINVAL, 25); a = args("child");
    a.string_bytes++; check(raw(&p) == -RUM_EINVAL, 26); a = args("child");
    p.path = UINT32_MAX; check(raw(&p) == -RUM_EFAULT, 27);
    char *path = (char *)PAGE;
    for (unsigned i = 0; i < 256; ++i) path[i] = 'a';
    p.path = PAGE; check(raw(&p) == -RUM_ENAMETOOLONG, 28);
    check(rum_run("/missing", &a, &r) == -RUM_ENOENT && rum_run("/data.bin", &a, &r) == -RUM_ENOEXEC &&
          rum_run("/disk/../../probe.elf", &a, &r) == -RUM_EINVAL, 29);
    struct rum_replace_request replace = {1, sizeof replace, (rum_address_t)(uintptr_t)"/data.bin", PAGE, 1, 0};
    replace.version = 2;
    check(rum_syscall3(RUM_SYS_REPLACE, (rum_address_t)(uintptr_t)&replace, 0, 0) == -RUM_EINVAL, 30);
    replace.version = 1; replace.reserved = 1;
    check(rum_syscall3(RUM_SYS_REPLACE, (rum_address_t)(uintptr_t)&replace, 0, 0) == -RUM_EINVAL, 31);
    check(rum_replace("/data.bin", (void *)UINT32_MAX, 1) == -RUM_EFAULT &&
          rum_replace("/data.bin", (void *)(PAGE + 3 * 4096 - 10), 20) == -RUM_EFAULT &&
          rum_replace("/data.bin", (void *)PAGE, 65537) == -RUM_E2BIG, 32);
    check(rum_console(2) == -RUM_EINVAL && rum_syscall3(RUM_SYS_CONSOLE, 0, 1, 0) == -RUM_EINVAL &&
          rum_syscall3(RUM_SYS_CONSOLE, 0, 0, 1) == -RUM_EINVAL, 33);
    rum_result_t file = rum_open("/data.bin", 1, 0); char original[4];
    check(file == 3 && rum_read(3, original, 4) == 4 && equal(original, "abc") && rum_close(3) == 0, 34);
    check(rum_replace("/empty", (void *)UINT32_MAX, 0) == 0 && rum_remove("/empty") == 0, 35);
    char *cross = (char *)(PAGE + 4096 - 16);
    for (unsigned i = 0; i < 700; ++i) cross[i] = (char)i;
    check(rum_replace("/copy.bin", cross, 700) == 0 && rum_remove("/copy.bin") == 0, 36);
}
int main(int argc, char **argv)
{
    check(argc >= 2, 1);
    if (equal(argv[1], "child") || equal(argv[1], "fault") || equal(argv[1], "blocking")) {
        check(argc == 3 && !argv[2][0] && rum_open("/data.bin", 1, 0) == 3, 2);
        char cwd[256]; check(rum_getcwd(cwd, sizeof cwd) > 0 && equal(cwd, "/disk/DOCS"), 3);
        check(rum_chdir("/") == 0, 4);
        if (equal(argv[1], "fault")) __asm__ volatile("ud2");
        if (equal(argv[1], "blocking")) { char key; rum_read(0, &key, 1); return 99; }
        return -37;
    }
    if (equal(argv[1], "normal")) invalid();
    check(rum_chdir("/disk/DOCS") == 0, 40);
    rum_result_t parent_file = rum_open("/data.bin", 1, 0); check(parent_file == 3, 41);
    struct rum_arguments a = args(equal(argv[1], "cancel") ? "blocking" :
                                  equal(argv[1], "normal") ? "nested" : "child");
    struct rum_process_result r = result();
    if (equal(argv[1], "normal")) {
        /* Arguments and output may each straddle a page; copy inputs first. */
        struct rum_arguments *cross = (void *)(PAGE + 4096 - 256); *cross = a;
        struct rum_process_result *out = (void *)(PAGE + 4096 - 12);
        /* Avoid overlapping the live argument packet. */
        check(rum_run("/probe.elf", cross, &r) == 0 && r.termination == RUM_PROCESS_EXITED && r.status == 0 && !r.fault_vector, 42);
        a = args("child");
        *out = result();
        check(rum_run("/probe.elf", &a, out) == 0 && out->status == -37 && !out->reserved, 43);
        a = args("fault"); r = result();
        check(rum_run("/probe.elf", &a, &r) == 0 && r.termination == RUM_PROCESS_FAULTED && r.fault_vector == 6 && !r.status, 44);
    } else {
        check(rum_run("/probe.elf", &a, &r) == 0 &&
              (equal(argv[1], "cancel") ? r.termination == RUM_PROCESS_CANCELLED : r.status == -37), 45);
    }
    char cwd[256]; check(rum_getcwd(cwd, sizeof cwd) > 0 && equal(cwd, "/disk/DOCS"), 46);
    check(rum_remove("/data.bin") == -RUM_EBUSY && rum_close((rum_handle_t)parent_file) == 0, 47);
    return 0;
}
