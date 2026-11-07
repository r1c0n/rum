#include <rum/tools.h>
static unsigned char job_pages[3 * 4096] __attribute__((aligned(4096)));

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
int main(int argc, char **argv)
{
    if (argc == 2 && equal(argv[1], "bgread")) {
        char c; struct rum_job_entry entry; uint32_t total;
        struct rum_arguments args = {0}; char *words[] = {"wflow", "stderr"};
        if (tool_arguments(&args, 2, words) || rum_read(RUM_STDIN, &c, 1) != -RUM_EACCES ||
            rum_job_list(&entry, 1, &total) != -RUM_EACCES || rum_job_create("forged", 0) != -RUM_EACCES ||
            rum_job_foreground(1) != -RUM_EACCES ||
            rum_spawn_with_handles("/RUM/WFLOW.ELF", &args, 0, 0, RUM_SPAWN_JOB) != -RUM_EACCES) return 60;
        tool_print("background input denied\n"); return 0;
    }
    if (argc == 2 && equal(argv[1], "focusread")) {
        const char ready[] = "reader awaiting foreground\n";
        if (rum_write(RUM_STDERR, ready, sizeof ready - 1) != sizeof ready - 1) return 61;
        char c; rum_result_t result;
        while ((result = rum_read(RUM_STDIN, &c, 1)) == -RUM_EACCES) (void)rum_sleep(5);
        if (result != 1) return 62;
        tool_print("reader got: "); tool_output(&c, 1); tool_print("\n"); return 0;
    }
    if (argc == 3 && equal(argv[1], "diskloop")) {
        rum_result_t fd = rum_open(argv[2], RUM_OPEN_WRITE, RUM_OPEN_CREATE);
        if (fd < 0) return 63;
        unsigned char bytes[512]; for (unsigned i = 0; i < sizeof bytes; ++i) bytes[i] = 0x5A;
        const char ready[] = "disk writer ready\n";
        (void)rum_write(RUM_STDERR, ready, sizeof ready - 1);
        for (;;) {
            if (rum_seek((rum_handle_t)fd, 0, RUM_SEEK_SET, 0) || rum_write((rum_handle_t)fd, bytes, sizeof bytes) != sizeof bytes) return 64;
            (void)rum_yield();
        }
    }
    if (argc == 3 && equal(argv[1], "jobabi")) {
        uint32_t id; if (tool_job_id(argv[2], &id)) return 65;
        struct rum_job_entry entry;
        for (unsigned i = 0; i < sizeof entry; ++i) ((unsigned char *)&entry)[i] = 0xA5;
        if (rum_job_wait(id, RUM_JOB_WAIT_NOHANG, &entry) != 0 || rum_job_release(id) != -RUM_EBUSY ||
            rum_job_ack(id) != -RUM_EACCES || rum_job_create("forged", 0) != -RUM_EACCES) return 66;
        for (unsigned i = 0; i < sizeof entry; ++i) if (((unsigned char *)&entry)[i] != 0xA5) return 67;
        struct rum_job_request request = { .version = RUM_JOB_ABI_VERSION, .size = sizeof request,
            .operation = RUM_JOB_LIST };
        rum_address_t address = (rum_address_t)(uintptr_t)&request;
        if (rum_syscall3(RUM_SYS_JOB, address, 1, 0) != -RUM_EINVAL ||
            rum_syscall3(RUM_SYS_JOB, UINT32_MAX, 0, 0) != -RUM_EFAULT) return 68;
        request.version = 99; if (rum_syscall3(RUM_SYS_JOB, address, 0, 0) != -RUM_EINVAL) return 69;
        request.version = 1; request.reserved[1] = 1;
        if (rum_syscall3(RUM_SYS_JOB, address, 0, 0) != -RUM_EINVAL) return 70;
        request.reserved[1] = 0; request.capacity = 9;
        if (rum_syscall3(RUM_SYS_JOB, address, 0, 0) != -RUM_EINVAL) return 71;
        request.capacity = 1; request.entries = address;
        if (rum_syscall3(RUM_SYS_JOB, address, 0, 0) != -RUM_EFAULT) return 72;
        request.entries = (rum_address_t)(uintptr_t)"readonly";
        if (rum_syscall3(RUM_SYS_JOB, address, 0, 0) != -RUM_EFAULT) return 73;
        request.capacity = 0; request.entries = 0; request.count = 1;
        if (rum_syscall3(RUM_SYS_JOB, address, 0, 0) != -RUM_EINVAL) return 74;
        request.count = 0;
        struct rum_job_request *cross = (void *)(job_pages + 4096 - 16);
        *cross = request; cross->entries = (rum_address_t)(uintptr_t)(job_pages + 8192 - 100); cross->capacity = 1;
        if (rum_syscall3(RUM_SYS_JOB, (rum_address_t)(uintptr_t)cross, 0, 0) != 1 || cross->count != 1 || !cross->total) return 75;
        if (rum_job_wait(id, 2, &entry) != -RUM_EINVAL ||
            rum_job_wait(id, 0, (void *)UINT32_MAX) != -RUM_EFAULT ||
            rum_job_wait(RUM_ABI_PID_MAX, RUM_JOB_WAIT_NOHANG, &entry) != -RUM_ESRCH) return 76;
        tool_print("job ABI validated\n"); return 0;
    }
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
