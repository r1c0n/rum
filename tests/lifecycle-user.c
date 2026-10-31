#include <rum/user.h>

#define PAGE 0x80800000u
int main(int argc, char **argv);
static struct rum_arguments arguments;
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static unsigned number(const char *text)
{
    unsigned value = 0;
    while (*text) value = value * 10u + (unsigned)(*text++ - '0');
    return value;
}
static void append(const char *text)
{
    arguments.offsets[arguments.argc++] = arguments.string_bytes;
    do { arguments.strings[arguments.string_bytes++] = *text; } while (*text++);
}
static void append_number(unsigned value)
{
    char reversed[11], text[11]; unsigned n = 0, length = 0;
    do { reversed[n++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    while (n) text[length++] = reversed[--n];
    text[length] = 0; append(text);
}
static void args(const char *mode, unsigned first, unsigned second)
{
    arguments = (struct rum_arguments){0};
    append("life"); append(mode); append_number(first); append_number(second);
}
static rum_result_t spawn(const char *mode, unsigned first, unsigned second)
{
    args(mode, first, second);
    return rum_spawn("/life.elf", &arguments);
}
static struct rum_process_result result(void)
{
    return (struct rum_process_result){ .version = RUM_PROCESS_ABI_VERSION,
        .size = sizeof(struct rum_process_result) };
}
static struct rum_process_result collect(rum_pid_t pid, unsigned kind, int status)
{
    struct rum_process_result output = result();
    check(rum_wait(pid, 0, &output) == 1 && output.process_id == pid &&
          output.termination == kind && output.status == status && !output.reserved &&
          output.fault_vector == (kind == RUM_PROCESS_FAULTED ? 6u : 0u), 100);
    return output;
}
static void ready(const char *path) { check(rum_replace(path, "ready", 5) == 0, 101); }
static void await(const char *path)
{
    for (;;) {
        rum_result_t file = rum_open(path, RUM_OPEN_READ, 0);
        if (file >= 0) { check(!rum_close((rum_handle_t)file), 102); return; }
        check(file == -RUM_ENOENT, 103);
    }
}
static rum_result_t raw(unsigned syscall, void *packet)
{
    return rum_syscall3(syscall, (rum_address_t)(uintptr_t)packet, 0, 0);
}
static void invalid(void)
{
    struct rum_process_entry entries[RUM_PROCESS_LIST_LIMIT]; uint32_t total = 0;
    rum_result_t count = rum_process_list(entries, RUM_PROCESS_LIST_LIMIT, &total);
    check(count >= 1 && (uint32_t)count == total && total <= RUM_PROCESS_LIST_LIMIT, 110);
    unsigned self = 0;
    for (int i = 0; i < count; ++i) {
        check(entries[i].process_id > 0 && entries[i].process_id <= RUM_ABI_PID_MAX &&
              entries[i].state >= RUM_PROCESS_RUNNABLE && entries[i].state <= RUM_PROCESS_ENDED &&
              !entries[i].name[RUM_PROCESS_NAME_CAPACITY - 1], 111);
        for (unsigned j = 0; entries[i].name[j]; ++j)
            check(entries[i].name[j] >= 32 && entries[i].name[j] <= 126, 112);
        if (entries[i].process_id == (rum_pid_t)rum_getpid()) {
            ++self; check(entries[i].state == RUM_PROCESS_RUNNING && entries[i].parent_id == 0, 113);
        }
    }
    check(self == 1 && rum_process_list(0, 0, &total) == 0 && total == (uint32_t)count, 114);
    struct rum_process_list_request q = {1, sizeof q, PAGE + 4096 - 40, 1, 0, 0, {0, 0}};
    unsigned char *canary = (void *)(uintptr_t)q.entries;
    for (unsigned i = 0; i < 160; ++i) canary[i] = 0x5a;
    check(raw(RUM_SYS_PROCESS_LIST, &q) == 1 && q.count == 1 && q.total == total, 115);
    for (unsigned i = 80; i < 160; ++i) check(canary[i] == 0x5a, 127);
    q.count = q.total = 0;
    check(raw(RUM_SYS_PROCESS_LIST, (void *)UINT32_MAX) == -RUM_EFAULT &&
          raw(RUM_SYS_PROCESS_LIST, (void *)(PAGE + 8192)) == -RUM_EFAULT &&
          rum_syscall3(RUM_SYS_PROCESS_LIST, 0, 1, 0) == -RUM_EINVAL &&
          rum_syscall3(RUM_SYS_PROCESS_LIST, 0, 0, 1) == -RUM_EINVAL, 116);
    q.version = 2; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 117); q.version = 1;
    --q.size; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 118); ++q.size;
    q.capacity = UINT32_MAX; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 119); q.capacity = 1;
    for (unsigned i = 0; i < 2; ++i) {
        q.reserved[i] = 1; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 120); q.reserved[i] = 0;
    }
    q.count = 1; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 121); q.count = 0;
    q.total = 1; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 122); q.total = 0;
    q.entries = UINT32_MAX; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EFAULT, 123);
    q.entries = PAGE + 8192 - 40; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EFAULT, 124);
    q.entries = (rum_address_t)(uintptr_t)&q; check(raw(RUM_SYS_PROCESS_LIST, &q) == -RUM_EINVAL, 125);
    q.entries = PAGE + 128;
    struct rum_process_list_request *cross = (void *)(PAGE + 4096 - 16); *cross = q;
    check(raw(RUM_SYS_PROCESS_LIST, cross) == 1 && cross->count == 1, 126);
    args("exit", 0, 0);
    struct rum_spawn_request s = {1, sizeof s, (rum_address_t)(uintptr_t)"/DOCS",
        (rum_address_t)(uintptr_t)&arguments, 0, {0, 0, 0}};
    struct rum_process_result out = result();
    struct rum_wait_request w = {1, sizeof w, RUM_WAIT_ANY, (rum_address_t)(uintptr_t)&out, 0, {0, 0, 0}};
    struct rum_terminate_request t = {1, sizeof t, 1, 0, {0, 0}};
    check(raw(RUM_SYS_SPAWN, (void *)UINT32_MAX) == -RUM_EFAULT &&
          raw(RUM_SYS_WAIT, (void *)UINT32_MAX) == -RUM_EFAULT &&
          raw(RUM_SYS_TERMINATE, (void *)UINT32_MAX) == -RUM_EFAULT, 10);
    for (unsigned call = RUM_SYS_SPAWN; call <= RUM_SYS_TERMINATE; ++call) {
        check(rum_syscall3(call, 0, 1, 0) == -RUM_EINVAL &&
              rum_syscall3(call, 0, 0, 1) == -RUM_EINVAL, 11);
    }
    s.version = 2; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 12); s.version = 1;
    --s.size; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 13); ++s.size;
    s.flags = 1; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 14); s.flags = 0;
    for (unsigned i = 0; i < 3; ++i) {
        s.reserved[i] = 1; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 15); s.reserved[i] = 0;
        w.reserved[i] = 1; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 16); w.reserved[i] = 0;
    }
    s.arguments = UINT32_MAX; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EFAULT, 17);
    s.arguments = PAGE + 3 * 4096 - 16; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EFAULT, 18);
    s.arguments = (rum_address_t)(uintptr_t)&arguments;
    arguments.argc = 33; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 19); args("exit", 0, 0);
    arguments.string_bytes = UINT32_MAX; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 20); args("exit", 0, 0);
    ++arguments.offsets[1]; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 21); args("exit", 0, 0);
    s.path = UINT32_MAX; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EFAULT, 22);
    char *path = (void *)PAGE;
    for (unsigned i = 0; i < RUM_ABI_PATH_CAPACITY; ++i) path[i] = 'x';
    s.path = PAGE; check(raw(RUM_SYS_SPAWN, &s) == -RUM_ENAMETOOLONG, 23);
    path[0] = 0; check(raw(RUM_SYS_SPAWN, &s) == -RUM_EINVAL, 24);
    w.version = 2; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 25); w.version = 1;
    --w.size; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 26); ++w.size;
    w.flags = 2; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 27); w.flags = 0;
    w.process_id = UINT32_MAX; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 28); w.process_id = 0;
    w.result = PAGE + 8192; check(raw(RUM_SYS_WAIT, &w) == -RUM_EFAULT, 29);
    w.result = PAGE + 8192 - 8; check(raw(RUM_SYS_WAIT, &w) == -RUM_EFAULT, 30);
    w.result = (rum_address_t)(uintptr_t)&out;
    out.version = 2; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 31); out = result();
    --out.size; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 32); out = result();
    out.reserved = 1; check(raw(RUM_SYS_WAIT, &w) == -RUM_EINVAL, 33); out = result();
    t.version = 2; check(raw(RUM_SYS_TERMINATE, &t) == -RUM_EINVAL, 34); t.version = 1;
    --t.size; check(raw(RUM_SYS_TERMINATE, &t) == -RUM_EINVAL, 35); ++t.size;
    t.flags = 2; check(raw(RUM_SYS_TERMINATE, &t) == -RUM_EINVAL, 36); t.flags = 0;
    for (unsigned i = 0; i < 2; ++i) {
        t.reserved[i] = 1; check(raw(RUM_SYS_TERMINATE, &t) == -RUM_EINVAL, 37); t.reserved[i] = 0;
    }
    check(rum_terminate(0, 0) == -RUM_EINVAL && rum_terminate(UINT32_MAX, 0) == -RUM_EINVAL &&
          rum_terminate(RUM_ABI_PID_MAX, 0) == -RUM_ESRCH &&
          rum_wait(RUM_ABI_PID_MAX, 0, &out) == -RUM_ECHILD &&
          rum_wait(0, RUM_WAIT_NOHANG, &out) == -RUM_ECHILD, 38);
}
static void exercise(unsigned unrelated)
{
    rum_pid_t self = (rum_pid_t)rum_getpid();
    check(rum_terminate(self, 0) == -RUM_EACCES && rum_terminate(unrelated, 0) == -RUM_EACCES, 40);
    struct rum_process_result out = result();
    check(rum_wait(unrelated, 0, &out) == -RUM_ECHILD && rum_wait(self, 0, &out) == -RUM_ECHILD, 41);
    check(rum_chdir("/DOCS") == 0 && rum_open("/data.bin", RUM_OPEN_READ, 0) == 3, 42);
    char byte; check(rum_read(3, &byte, 1) == 1 && byte == 'a', 43);
    rum_result_t spin = spawn("spin", 0, 0); check(spin > 0, 44);
    out.status = 123;
    check(rum_wait((rum_pid_t)spin, RUM_WAIT_NOHANG, &out) == 0 && out.status == 123 && !out.process_id, 45);
    rum_result_t exited = spawn("exit", 0, 0); check(exited > 0, 46);
    out = result();
    check(rum_wait(0, 0, &out) == 1 && out.process_id == (rum_pid_t)exited &&
          out.termination == RUM_PROCESS_EXITED && out.status == INT32_MIN, 47);
    check(rum_wait((rum_pid_t)exited, 0, &out) == -RUM_ECHILD &&
          rum_terminate((rum_pid_t)exited, 0) == -RUM_ESRCH, 48);
    check(rum_terminate((rum_pid_t)spin, RUM_TERMINATE_SESSION) == -RUM_EACCES &&
          rum_terminate((rum_pid_t)spin, 0) == 0 && rum_terminate((rum_pid_t)spin, 0) == 0, 49);
    /* Bad outputs never collect the child; a subsequent valid wait succeeds. */
    check(rum_wait((rum_pid_t)spin, 0, (void *)UINT32_MAX) == -RUM_EFAULT &&
          rum_wait((rum_pid_t)spin, 0, (void *)(PAGE + 8192 - 8)) == -RUM_EFAULT, 50);
    collect((rum_pid_t)spin, RUM_PROCESS_CANCELLED, 0);
    rum_result_t attacker = spawn("attack", unrelated, self); check(attacker > 0, 51);
    check(attacker != exited && rum_wait((rum_pid_t)exited, RUM_WAIT_NOHANG, &out) == -RUM_ECHILD &&
          rum_terminate((rum_pid_t)exited, 0) == -RUM_ESRCH, 84);
    collect((rum_pid_t)attacker, RUM_PROCESS_EXITED, 0);
    rum_result_t probe = spawn("probe", 0, 0); check(probe > 0, 52);
    collect((rum_pid_t)probe, RUM_PROCESS_EXITED, -37);
    char cwd[256];
    check(rum_getcwd(cwd, sizeof cwd) > 0 && equal(cwd, "/DOCS") &&
          rum_read(3, &byte, 1) == 1 && byte == 'b', 53);
    rum_result_t fault = spawn("fault", 0, 0); check(fault > 0, 54);
    collect((rum_pid_t)fault, RUM_PROCESS_FAULTED, 0);
    rum_result_t input = spawn("input", 0, 0); check(input > 0, 55);
    collect((rum_pid_t)input, RUM_PROCESS_EXITED, 0);
    args("exit", 0, 0);
    struct rum_arguments *cross_args = (void *)(PAGE + 4096 - 256); *cross_args = arguments;
    rum_result_t cross = rum_spawn("/life.elf", cross_args); check(cross > 0, 56);
    struct rum_process_result *cross_out = (void *)(PAGE + 4096 - 12); *cross_out = result();
    check(rum_wait((rum_pid_t)cross, 0, cross_out) == 1 && cross_out->status == INT32_MIN, 57);
    struct rum_spawn_request *cross_request = (void *)(PAGE + 4096 - 8);
    *cross_request = (struct rum_spawn_request){1, sizeof *cross_request,
        (rum_address_t)(uintptr_t)"/life.elf", (rum_address_t)(uintptr_t)&arguments, 0, {0, 0, 0}};
    cross = raw(RUM_SYS_SPAWN, cross_request); check(cross > 0, 58);
    collect((rum_pid_t)cross, RUM_PROCESS_EXITED, INT32_MIN);
    check(rum_spawn("/missing", &arguments) == -RUM_ENOENT &&
          rum_spawn("/data.bin", &arguments) == -RUM_ENOEXEC &&
          rum_spawn("/bad name/life.elf", &arguments) == -RUM_EINVAL, 59);
    /* Fill every remaining slot with non-yielding children, then reclaim all. */
    rum_pid_t children[20]; unsigned count = 0;
    for (;;) {
        rum_result_t pid = spawn("spin", 0, 0);
        if (pid < 0) { check(pid == -RUM_ENOMEM && count > 0, 60); break; }
        check(count < 20 && (!count || (rum_pid_t)pid > children[count - 1]), 61);
        children[count++] = (rum_pid_t)pid;
    }
    for (unsigned i = 0; i < count; ++i) {
        check(rum_terminate(children[i], 0) == 0, 62);
        collect(children[i], RUM_PROCESS_CANCELLED, 0);
    }
    check(rum_wait(0, RUM_WAIT_NOHANG, &out) == -RUM_ECHILD && rum_close(3) == 0, 63);
    for (unsigned action = 0; action < 3; ++action) {
        rum_result_t leaver = spawn("leaver", action, 0); check(leaver > 0, 64);
        if (action == 2) { await("/cancel.RDY"); check(!rum_terminate((rum_pid_t)leaver, 0), 65); }
        collect((rum_pid_t)leaver, action == 0 ? RUM_PROCESS_EXITED :
                action == 1 ? RUM_PROCESS_FAULTED : RUM_PROCESS_CANCELLED, action ? 0 : 77);
        /* A foreground RUN gives the cancelled descendants scheduling turns
           before the next tree is constructed. It uses the unchanged packet. */
        args("return", 0, 0); out = result();
        check(!rum_run("/life.elf", &arguments, &out) && !out.status, 66);
        check(!rum_remove("/leaf.RDY") && !rum_remove("/tree.RDY"), 67);
        if (action == 2) check(!rum_remove("/cancel.RDY"), 68);
    }
}
int main(int argc, char **argv)
{
    check(argc >= 4, 1);
    unsigned first = number(argv[2]), second = number(argv[3]);
    if (equal(argv[1], "invalid")) { invalid(); return 0; }
    if (equal(argv[1], "normal")) { exercise(first); return 0; }
    if (equal(argv[1], "oom")) {
        rum_result_t error = spawn("exit", 0, 0);
        check(error == -RUM_ENOMEM || error == -RUM_ENOEXEC, 69); return 0;
    }
    if (equal(argv[1], "return")) return 0;
    if (equal(argv[1], "copywait")) {
        rum_result_t child = spawn("waitfile", 0, 0); check(child > 0, 82);
        struct rum_process_result *out = (void *)PAGE; *out = result(); out->status = 123;
        /* The trusted fixture revokes output write permission while WAIT is
           asleep, then lets the child exit. Retry must still find its record. */
        check(rum_wait((rum_pid_t)child, 0, out) == -RUM_EFAULT && out->status == 123, 83);
        collect((rum_pid_t)child, RUM_PROCESS_EXITED, 0); return 0;
    }
    if (equal(argv[1], "waitfile")) { await("/finish.CHD"); return 0; }
    if (equal(argv[1], "session")) {
        rum_result_t sibling = spawn("spin", 0, 0); check(sibling > 0, 70);
        args("delegate", (unsigned)sibling, first);
        append_number((unsigned)rum_getpid());
        struct rum_process_result out = result();
        check(!rum_run_command("/life.elf", &arguments, "", &out) && !out.status, 71);
        collect((rum_pid_t)sibling, RUM_PROCESS_CANCELLED, 0); return 0;
    }
    if (equal(argv[1], "delegate")) {
        check(rum_terminate(first, 0) == -RUM_EACCES &&
              rum_terminate(second, RUM_TERMINATE_SESSION) == -RUM_EACCES &&
              argc == 5 && rum_terminate(number(argv[4]), RUM_TERMINATE_SESSION) == -RUM_EACCES &&
              !rum_terminate(first, RUM_TERMINATE_SESSION), 72); return 0;
    }
    if (equal(argv[1], "attack")) {
        struct rum_process_result out = result();
        check(rum_terminate(first, 0) == -RUM_EACCES && rum_terminate(second, 0) == -RUM_EACCES &&
              rum_terminate(first, RUM_TERMINATE_SESSION) == -RUM_EACCES &&
              rum_wait(first, 0, &out) == -RUM_ECHILD && rum_wait(second, 0, &out) == -RUM_ECHILD, 73);
        return 0;
    }
    if (equal(argv[1], "input")) {
        char key;
        check(rum_read(0, &key, 1) == -RUM_EACCES && rum_console(RUM_CONSOLE_CLEAR) == -RUM_EACCES, 74);
        args("return", 0, 0); struct rum_process_result out = result();
        check(rum_run("/life.elf", &arguments, &out) == -RUM_EBUSY, 75); return 0;
    }
    if (equal(argv[1], "leaver")) {
        rum_result_t branch = spawn("branch", 0, 0);
        check(branch > 0 && spawn("exit", 0, 0) > 0, 76);
        await("/tree.RDY");
        if (first == 1) __asm__ volatile ("ud2");
        if (first == 2) { ready("/cancel.RDY"); struct rum_process_result out = result(); rum_wait((rum_pid_t)branch, 0, &out); for (;;) {} }
        return 77;
    }
    if (equal(argv[1], "branch")) {
        rum_result_t leaf = spawn("leaf", 0, 0); check(leaf > 0, 77);
        await("/leaf.RDY"); ready("/tree.RDY");
        struct rum_process_result out = result(); rum_wait((rum_pid_t)leaf, 0, &out); for (;;) {}
    }
    if (equal(argv[1], "orphan")) {
        check(spawn("branch", 0, 0) > 0 && spawn("exit", 0, 0) > 0, 78);
        await("/tree.RDY"); return 0;
    }
    check(rum_open("/data.bin", RUM_OPEN_READ, 0) == 3, 79);
    if (equal(argv[1], "exit")) return INT32_MIN;
    if (equal(argv[1], "fault")) __asm__ volatile ("ud2");
    if (equal(argv[1], "probe")) {
        char cwd[256], bytes[3];
        check(rum_getcwd(cwd, sizeof cwd) > 0 && equal(cwd, "/DOCS") &&
              rum_read(3, bytes, 3) == 3 && bytes[0] == 'a' && !rum_chdir("/"), 80);
        return -37;
    }
    check(!rum_chdir("/DOCS"), 81);
    if (equal(argv[1], "leaf")) ready("/leaf.RDY");
    for (;;) __asm__ volatile ("pause");
}
