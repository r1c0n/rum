#include <rum/user.h>
#include <stdio.h>
#include <string.h>
#define TEST_QUERY 0xffffffe0u
#define PAGE 0x80800000u
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static uint32_t query(unsigned which, uint32_t argument) { return rum_syscall3(TEST_QUERY, which, argument, 0); }
static struct rum_process_object_information output(void)
{
    return (struct rum_process_object_information){ .version = 1, .size = sizeof(struct rum_process_object_information) };
}
static rum_pid_t spawn(const char *mode, uint32_t value)
{
    struct rum_arguments args = { .argc = 3, .offsets = {0, 10}, .string_bytes = 10, .strings = "/PROC.ELF" };
    size_t length = strlen(mode) + 1;
    memcpy(args.strings + args.string_bytes, mode, length); args.string_bytes += length;
    args.offsets[2] = args.string_bytes;
    int bytes = snprintf(args.strings + args.string_bytes, 16, "%08x", value);
    check(bytes == 8, 1); args.string_bytes += 9;
    rum_result_t pid = rum_spawn("/PROC.ELF", &args); check(pid > 0, 2); return (rum_pid_t)pid;
}
static uint32_t number(const char *text)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned digit = (unsigned char)text[i]; digit = digit <= '9' ? digit - '0' : digit - 'a' + 10;
        check(digit < 16, 3); value = value * 16 + digit;
    }
    check(!text[8], 4); return value;
}
static void collect(rum_pid_t pid, unsigned termination, int status)
{
    struct rum_process_result result = { .version = 1, .size = sizeof result };
    check(rum_wait(pid, 0, &result) == 1 && result.termination == termination && result.status == status, 5);
}
static rum_object_handle_t open_process(rum_pid_t pid, unsigned rights)
{
    rum_object_handle_t handle;
    check(!rum_process_open(pid, rights, 0, &handle), 6); return handle;
}
static void wait_operations(unsigned count)
{
    for (unsigned i = 0; i < 1000; ++i) {
        if (query(1, 0) == count) return;
        check(!rum_yield(), 7);
    }
    check(0, 8);
}
static void invalid(rum_pid_t foreign)
{
    rum_object_handle_t handle = 19;
    check(rum_process_open(0, RUM_RIGHT_QUERY, 0, &handle) == -RUM_EINVAL && handle == 19, 10);
    check(rum_process_open(RUM_ABI_PID_MAX, RUM_RIGHT_QUERY, 0, &handle) == -RUM_ESRCH, 11);
    check(rum_process_open(foreign, RUM_RIGHT_PROCESS, 0, &handle) == -RUM_EACCES &&
          rum_process_open(foreign, RUM_RIGHT_QUERY, RUM_PROCESS_OPEN_SESSION, &handle) == -RUM_EACCES, 12);
    rum_pid_t self = (rum_pid_t)rum_getpid();
    check(rum_process_open(self, RUM_RIGHT_WAIT, 0, &handle) == -RUM_EACCES &&
          rum_process_open(self, RUM_RIGHT_TERMINATE, 0, &handle) == -RUM_EACCES, 13);
    rum_object_handle_t own = open_process(self, RUM_RIGHT_COMMON);
    struct rum_process_object_information info = output();
    check(!rum_process_query(own, &info) && info.process_id == self && info.state == RUM_PROCESS_RUNNING &&
          !info.termination && !info.status && !info.fault_vector && !info.reserved[0], 14);
    struct rum_process_object_request request = { .version = 1, .size = sizeof request,
        .operation = RUM_PROCESS_OBJECT_QUERY, .handle = own, .data = (rum_address_t)(uintptr_t)&info };
    request.version = 2; check(rum_process_object_control(&request) == -RUM_EINVAL, 15); request.version = 1;
    request.size--; check(rum_process_object_control(&request) == -RUM_EINVAL, 16); request.size++;
    request.reserved[2] = 1; check(rum_process_object_control(&request) == -RUM_EINVAL, 17); request.reserved[2] = 0;
    request.flags = 2; check(rum_process_object_control(&request) == -RUM_EINVAL, 18); request.flags = 0;
    request.result_handle = 1; check(rum_process_object_control(&request) == -RUM_EINVAL, 19); request.result_handle = 0;
    request.rights = RUM_RIGHT_QUERY; check(rum_process_object_control(&request) == -RUM_EINVAL, 20); request.rights = 0;
    check(rum_syscall3(RUM_SYS_PROCESS_OBJECT, (rum_address_t)(uintptr_t)&request, 1, 0) == -RUM_EINVAL &&
          rum_process_object_control(NULL) == -RUM_EFAULT, 21);
    request.data = (rum_address_t)(uintptr_t)&request;
    check(rum_process_object_control(&request) == -RUM_EINVAL, 22);
    struct rum_process_object_request *cross = (void *)(PAGE + 4096 - 20);
    request.data = PAGE + 4096 + 100; *cross = request;
    *(struct rum_process_object_information *)(uintptr_t)request.data = output();
    check(!rum_process_object_control(cross), 23);
    request.data = PAGE + 8192 - 20;
    check(rum_process_object_control(&request) == -RUM_EFAULT &&
          rum_process_object_control((void *)(PAGE + 8192 - 20)) == -RUM_EFAULT, 24);
    info = output();
    check(rum_process_wait(own, RUM_PROCESS_OBJECT_NOHANG, &info) == -RUM_EACCES &&
          rum_process_terminate(own) == -RUM_EACCES && !rum_object_close(own), 25);
    rum_result_t file = rum_open("/PROC.ELF", RUM_OPEN_READ, 0);
    check(file >= 3 && !rum_object_from_descriptor((rum_handle_t)file, RUM_OBJECT_FILE, RUM_RIGHT_QUERY, 0, &handle) &&
          rum_process_query(handle, &info) == -RUM_EINVAL && !rum_object_close(handle) && !rum_close((rum_handle_t)file), 26);
}
static void retained(void)
{
    uint32_t pages = query(5, 0);
    rum_pid_t pid = spawn("exit", 0);
    rum_object_handle_t handle = open_process(pid, RUM_RIGHT_PROCESS), alias;
    check(!rum_object_duplicate(handle, RUM_OBJECT_PROCESS, RUM_RIGHT_QUERY | RUM_RIGHT_WAIT, 0, &alias), 30);
    struct rum_process_object_information info = output();
    check(rum_process_terminate(alias) == -RUM_EACCES && rum_process_wait(handle, 0, &info) == 1 &&
          info.process_id == pid && info.termination == RUM_PROCESS_EXITED && info.status == 17, 31);
    collect(pid, RUM_PROCESS_EXITED, 17);
    check(query(5, 0) == pages, 42);
    /* The test kernel deliberately reuses both PID and execution ID. */
    check(query(2, pid) == pid, 32);
    rum_pid_t replacement = spawn("spin", 0); check(replacement == pid, 33);
    check(!rum_process_terminate(handle) && rum_process_wait(alias, 0, &info) == 1 && info.status == 17, 34);
    check(!rum_sleep(60), 43); /* A misdirected cancellation would run now. */
    struct rum_process_result pending = { .version = 1, .size = sizeof pending };
    check(rum_wait(replacement, RUM_WAIT_NOHANG, &pending) == 0, 35);
    rum_object_handle_t fresh = open_process(replacement, RUM_RIGHT_PROCESS);
    info = output(); struct rum_process_object_information saved = info;
    check(rum_process_wait(fresh, RUM_PROCESS_OBJECT_NOHANG, &info) == 0 && !memcmp(&info, &saved, sizeof info), 36);
    check(!rum_object_close(fresh) && rum_wait(replacement, RUM_WAIT_NOHANG, &pending) == 0, 37);
    fresh = open_process(replacement, RUM_RIGHT_PROCESS);
    check(!rum_process_terminate(fresh) && rum_process_wait(fresh, 0, &info) == 1 && info.termination == RUM_PROCESS_CANCELLED, 38);
    collect(replacement, RUM_PROCESS_CANCELLED, 0);
    check(!rum_process_query(handle, &info) && info.status == 17 && !rum_object_close(handle) &&
          !rum_object_close(alias) && !rum_object_close(fresh), 39);
    pid = spawn("fault", 0); handle = open_process(pid, RUM_RIGHT_QUERY | RUM_RIGHT_WAIT);
    check(rum_process_wait(handle, 0, &info) == 1 && info.termination == RUM_PROCESS_FAULTED && info.fault_vector == 6, 40);
    collect(pid, RUM_PROCESS_FAULTED, 0); check(!rum_object_close(handle), 41);
}
static void blocked(void)
{
    rum_pid_t target = spawn("spin", 0);
    rum_object_handle_t handle = open_process(target, RUM_RIGHT_PROCESS), inherited;
    check(!rum_object_duplicate(handle, RUM_OBJECT_PROCESS, RUM_RIGHT_WAIT, RUM_OBJECT_INHERIT, &inherited), 50);
    check(query(4, 0) == 1, 62);
    rum_pid_t observer = spawn("wait", inherited);
    check(!rum_object_close(inherited), 51); wait_operations(1);
    check(!rum_terminate(observer, 0), 52); collect(observer, RUM_PROCESS_CANCELLED, 0);
    check(!query(1, 0) && !rum_process_terminate(handle), 53);
    collect(target, RUM_PROCESS_CANCELLED, 0); check(!rum_object_close(handle), 54);
    struct rum_pipe_request pipe = { .version = 1, .size = sizeof pipe };
    check(!rum_pipe(&pipe), 55);
    struct rum_arguments args = { .argc = 3, .offsets = {0, 10, 15}, .string_bytes = 24,
        .strings = "/PROC.ELF\0pipe\0" "00000000" };
    struct rum_handle_mapping mapping = {pipe.read_handle, 0, {0}};
    rum_result_t child = rum_spawn_with_handles("/PROC.ELF", &args, &mapping, 1, 0); check(child > 0, 56);
    check(!rum_close(pipe.read_handle), 57);
    for (unsigned i = 0; i < 1000 && !query(0, 0); ++i) check(!rum_yield(), 58);
    check(query(0, 0) == 1, 59); handle = open_process((rum_pid_t)child, RUM_RIGHT_PROCESS);
    check(!rum_process_terminate(handle), 60); collect((rum_pid_t)child, RUM_PROCESS_CANCELLED, 0);
    check(!query(0, 0) && !query(1, 0) && !rum_close(pipe.write_handle) && !rum_object_close(handle), 61);
}
static void exhaustion(void)
{
    rum_object_handle_t all[32];
    for (unsigned i = 0; i < 32; ++i) all[i] = open_process((rum_pid_t)rum_getpid(), RUM_RIGHT_QUERY);
    unsigned before = query(3, 0);
    rum_pid_t child = spawn("exit", 0); rum_object_handle_t untouched = 19;
    check(rum_process_open(child, RUM_RIGHT_QUERY, 0, &untouched) == -RUM_EMFILE && untouched == 19 && query(3, 0) == before, 70);
    collect(child, RUM_PROCESS_EXITED, 17);
    for (unsigned i = 0; i < 32; ++i) check(!rum_object_close(all[i]), 71);
}
int main(int argc, char **argv)
{
    check(argc == 3, 80);
    uint32_t value = number(argv[2]);
    if (!strcmp(argv[1], "exit")) return 17;
    if (!strcmp(argv[1], "spin")) for (;;) __asm__ volatile("pause");
    if (!strcmp(argv[1], "fault")) __asm__ volatile("ud2");
    if (!strcmp(argv[1], "pipe")) { char byte; (void)rum_read(0, &byte, 1); return 81; }
    if (!strcmp(argv[1], "wait")) {
        struct rum_process_object_information info = output();
        check(rum_process_terminate(value) == -RUM_EACCES, 82);
        (void)rum_process_wait(value, 0, &info); return 83;
    }
    if (!strcmp(argv[1], "cancel")) {
        rum_pid_t child = spawn("spin", 0);
        rum_object_handle_t handle = open_process(child, RUM_RIGHT_WAIT);
        struct rum_process_object_information info = output();
        (void)rum_process_wait(handle, 0, &info); return 84;
    }
    invalid(value); retained(); blocked(); exhaustion(); return 0;
}
