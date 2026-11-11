#include <rum/user.h>
#include <stdbool.h>

#define PAGE 0x80800000u
#define TEST_QUERY 0xffffffe0u
#define PAYLOAD (RUM_PIPE_CAPACITY * 9 + 173)
int main(int argc, char **argv);
static struct rum_arguments arguments;
static unsigned char buffer[1024];
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; } return *a == *b;
}
static unsigned char pattern(unsigned i) { return (unsigned char)((i * 37u) ^ (i >> 7) ^ 0x5a); }
static struct rum_pipe_request create(void)
{
    struct rum_pipe_request pipe = { .version = RUM_PIPE_ABI_VERSION, .size = sizeof pipe };
    check(rum_pipe(&pipe) == 0 && pipe.capacity == RUM_PIPE_CAPACITY &&
          pipe.atomic_write == RUM_PIPE_ATOMIC_WRITE && pipe.read_handle != pipe.write_handle, 1);
    return pipe;
}
static rum_result_t spawn(const char *mode, rum_handle_t source, rum_handle_t target)
{
    arguments = (struct rum_arguments){ .argc = 2, .string_bytes = 10, .offsets = {0, 10}, .strings = "/PIPE.ELF" };
    do { arguments.strings[arguments.string_bytes++] = *mode; } while (*mode++);
    struct rum_handle_mapping map = {source, target, {0}};
    return rum_spawn_with_handles("/PIPE.ELF", &arguments, &map, 1, RUM_SPAWN_NO_DEFAULT_STREAMS);
}
static int32_t collect(rum_result_t pid, unsigned termination)
{
    struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
    check(pid > 0 && rum_wait((rum_pid_t)pid, 0, &child) == 1 && child.process_id == (rum_pid_t)pid &&
          child.termination == termination, 2);
    return child.status;
}
static void finished(rum_result_t pid) { check(collect(pid, RUM_PROCESS_EXITED) == 0, 3); }
static rum_result_t query(unsigned what) { return rum_syscall3(TEST_QUERY, what, 0, 0); }
static void waiters(unsigned what, unsigned count)
{
    for (unsigned i = 0; i < 1000; ++i) {
        if (query(what) == (rum_result_t)count) return;
        check(rum_yield() == 0, 4);
    }
    check(0, 5);
}
static void pending(rum_result_t pid)
{
    struct rum_process_result child = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child };
    check(rum_wait((rum_pid_t)pid, RUM_WAIT_NOHANG, &child) == 0, 6);
}
static void fill(rum_handle_t writer)
{
    for (unsigned i = 0; i < RUM_PIPE_ATOMIC_WRITE; ++i) buffer[i] = pattern(i);
    for (unsigned i = 0; i < RUM_PIPE_CAPACITY; i += RUM_PIPE_ATOMIC_WRITE)
        check(rum_write(writer, buffer, RUM_PIPE_ATOMIC_WRITE) == RUM_PIPE_ATOMIC_WRITE, 7);
}
static void invalid(void)
{
    struct rum_pipe_request request = {1, sizeof request, 0, 0, 0, 0, 0, 0};
    rum_result_t descriptors = query(5);
    check(rum_syscall3(RUM_SYS_PIPE, UINT32_MAX, 0, 0) == -RUM_EFAULT, 10);
    check(rum_syscall3(RUM_SYS_PIPE, PAGE + 8192 - 8, 0, 0) == -RUM_EFAULT, 11);
    request.version = 2; check(rum_pipe(&request) == -RUM_EINVAL, 12); request.version = 1;
    request.size--; check(rum_pipe(&request) == -RUM_EINVAL, 13); request.size++;
    uint32_t *words = (void *)&request;
    for (unsigned i = 2; i < 8; ++i) { words[i] = 1; check(rum_pipe(&request) == -RUM_EINVAL, 14); words[i] = 0; }
    check(rum_syscall3(RUM_SYS_PIPE, (rum_address_t)(uintptr_t)&request, 1, 0) == -RUM_EINVAL, 15);
    check(query(0) == 0 && query(5) == descriptors, 16);
    struct rum_pipe_request *cross = (void *)(PAGE + 4096 - 16);
    *cross = request;
    check(rum_pipe(cross) == 0 && cross->read_handle == 3 && cross->write_handle == 4, 17);
    rum_handle_t read = cross->read_handle, write = cross->write_handle;
    check(rum_pipe(cross) == -RUM_EINVAL, 18); /* Outputs must start zero. */
    check(rum_read(write, buffer, 0) == -RUM_EBADF && rum_write(read, buffer, 0) == -RUM_EBADF &&
          rum_read(read, (void *)UINT32_MAX, 0) == 0 && rum_write(write, (void *)UINT32_MAX, 0) == 0, 19);
    check(rum_read(32, buffer, 0) == -RUM_EBADF, 20);
    check(rum_seek(read, 0, 0, 0) == -RUM_ESPIPE && rum_seek(write, 0, 0, 0) == -RUM_ESPIPE && rum_flush(write) == -RUM_EINVAL, 21);
    struct rum_directory_entry entry = { .version = RUM_FS_ABI_VERSION, .size = sizeof entry };
    check(rum_readdir(read, &entry) == -RUM_ENOTDIR && rum_readdir(write, &entry) == -RUM_ENOTDIR, 22);
    unsigned char *bytes = (void *)(PAGE + 4096 - 17);
    for (unsigned i = 0; i < 600; ++i) bytes[i] = pattern(i);
    check(rum_write(write, (void *)(PAGE + 12288 - 16), 32) == -RUM_EFAULT && query(4) == 0, 23);
    check(rum_write(write, bytes, 600) == RUM_PIPE_ATOMIC_WRITE, 24);
    check(rum_read(read, (void *)(PAGE + 8192 - 16), 32) == -RUM_EFAULT && query(4) == RUM_PIPE_ATOMIC_WRITE, 25);
    for (unsigned i = 0; i < 600; ++i) bytes[i] = 0xcc;
    check(rum_read(read, bytes, 600) == RUM_PIPE_ATOMIC_WRITE, 26);
    for (unsigned i = 0; i < RUM_PIPE_ATOMIC_WRITE; ++i) check(bytes[i] == pattern(i), 27);
    check(bytes[RUM_PIPE_ATOMIC_WRITE] == 0xcc && query(4) == 0, 28);
    struct rum_handle_mapping map = {write, 1, {0}};
    arguments = (struct rum_arguments){ .argc = 1, .string_bytes = 10, .strings = "/PIPE.ELF" };
    check(rum_spawn_with_handles("/ABSENT.ELF", &arguments, &map, 1, 0) == -RUM_ENOENT, 29);
    check(rum_close(write) == 0 && rum_read(read, buffer, 1) == 0 && rum_close(read) == 0 && query(0) == 0, 30);
}
static rum_object_handle_t exported(rum_handle_t descriptor, bool write)
{
    rum_object_handle_t handle;
    check(!rum_object_from_descriptor(descriptor, write ? RUM_OBJECT_PIPE_WRITER : RUM_OBJECT_PIPE_READER,
          RUM_RIGHT_COMMON | (write ? RUM_RIGHT_WRITE : RUM_RIGHT_READ), 0, &handle) && !rum_close(descriptor), 71);
    return handle;
}
static void binary(bool typed)
{
    struct rum_pipe_request pipe = create();
    rum_result_t producer = spawn("producer", pipe.write_handle, 1);
    check(producer > 0 && rum_close(pipe.write_handle) == 0, 31);
    rum_object_handle_t handle = typed ? exported(pipe.read_handle, false) : 0;
    unsigned total = 0; rum_result_t count;
    while ((count = typed ? rum_object_read(handle, buffer, 333) : rum_read(pipe.read_handle, buffer, 333)) > 0) {
        check(total + (unsigned)count <= PAYLOAD, 32);
        for (unsigned i = 0; i < (unsigned)count; ++i) check(buffer[i] == pattern(total + i), 33);
        total += (unsigned)count;
    }
    check(count == 0 && total == PAYLOAD && (typed ? rum_object_close(handle) : rum_close(pipe.read_handle)) == 0, 34); finished(producer);
}
static void writers(void)
{
    struct rum_pipe_request pipe = create();
    rum_result_t a = spawn("recordsA", pipe.write_handle, 1), b = spawn("recordsB", pipe.write_handle, 1);
    check(a > 0 && b > 0 && rum_close(pipe.write_handle) == 0, 35);
    unsigned positions[2] = {0}, fragment = 0, writer = 0, record = 0, total = 0;
    rum_result_t count;
    while ((count = rum_read(pipe.read_handle, buffer, 211)) > 0) {
        for (unsigned i = 0; i < (unsigned)count; ++i) {
            unsigned char byte = buffer[i];
            if (!fragment) { check(byte == 'A' || byte == 'B', 36); writer = byte == 'B'; record = positions[writer]; }
            else if (fragment == 1) check(byte == record, 37);
            else check(byte == (unsigned char)(('A' + writer) ^ record ^ fragment), 38);
            if (++fragment == RUM_PIPE_ATOMIC_WRITE) { fragment = 0; ++positions[writer]; }
            ++total;
        }
    }
    check(!count && !fragment && positions[0] == 16 && positions[1] == 16 && total == 32 * RUM_PIPE_ATOMIC_WRITE, 39);
    check(rum_close(pipe.read_handle) == 0, 40); finished(a); finished(b);
}
static void readers(void)
{
    struct rum_pipe_request pipe = create();
    rum_result_t a = spawn("consumer", pipe.read_handle, 0), b = spawn("consumer", pipe.read_handle, 0);
    check(a > 0 && b > 0 && rum_close(pipe.read_handle) == 0, 41); waiters(1, 2);
    for (unsigned i = 0; i < sizeof buffer; ++i) buffer[i] = 'R';
    unsigned total = 0;
    while (total < RUM_PIPE_CAPACITY * 3) {
        rum_result_t count = rum_write(pipe.write_handle, buffer, sizeof buffer);
        check(count > 0 && count <= (rum_result_t)RUM_PIPE_ATOMIC_WRITE, 42); total += (unsigned)count;
    }
    check(rum_close(pipe.write_handle) == 0, 43);
    int32_t first = collect(a, RUM_PROCESS_EXITED), second = collect(b, RUM_PROCESS_EXITED);
    check(first > 0 && second > 0 && first + second == RUM_PIPE_CAPACITY * 3, 44);
}
static void closures(bool write, bool typed)
{
    struct rum_pipe_request pipe = create();
    if (write) fill(pipe.write_handle);
    rum_result_t child = spawn(typed ? (write ? "typedwrite" : "typedread") : (write ? "write-epipe" : "read-eof"),
        write ? pipe.write_handle : pipe.read_handle, write ? 1 : 0);
    check(child > 0 && rum_close(write ? pipe.write_handle : pipe.read_handle) == 0, 45);
    waiters(write ? 2 : 1, 1);
    rum_handle_t remaining = write ? pipe.read_handle : pipe.write_handle;
    rum_result_t alias = rum_duplicate(remaining, RUM_DUP_ANY, 0);
    check(alias >= 0 && rum_close(remaining) == 0, 46); pending(child);
    check(query(write ? 2 : 1) == 1 && rum_close((rum_handle_t)alias) == 0, 47);
    finished(child);
}
static void cancellation(bool write, bool typed)
{
    struct rum_pipe_request pipe = create();
    if (write) fill(pipe.write_handle);
    rum_result_t child = spawn(typed ? (write ? "typedwrite" : "typedread") : (write ? "write-epipe" : "read-eof"),
        write ? pipe.write_handle : pipe.read_handle, write ? 1 : 0);
    check(child > 0 && rum_close(write ? pipe.write_handle : pipe.read_handle) == 0, 48);
    waiters(write ? 2 : 1, 1);
    check(rum_terminate((rum_pid_t)child, 0) == 0 && collect(child, RUM_PROCESS_CANCELLED) == 0, 49);
    if (write) {
        unsigned total = 0; rum_result_t count;
        while ((count = rum_read(pipe.read_handle, buffer, sizeof buffer)) > 0) {
            for (unsigned i = 0; i < (unsigned)count; ++i) check(buffer[i] == pattern((total + i) % RUM_PIPE_ATOMIC_WRITE), 50);
            total += (unsigned)count;
        }
        check(!count && total == RUM_PIPE_CAPACITY && rum_close(pipe.read_handle) == 0, 51);
    } else {
        check(rum_write(pipe.write_handle, "x", 1) == -RUM_EPIPE && rum_write(pipe.write_handle, NULL, 0) == 0 &&
              rum_close(pipe.write_handle) == 0, 52);
    }
    check(query(0) == 0 && query(3) == 0, 53);
}
static void fault(void)
{
    struct rum_pipe_request pipe = create();
    rum_result_t child = spawn("fault", pipe.write_handle, 1);
    check(child > 0 && rum_close(pipe.write_handle) == 0, 54);
    unsigned total = 0; rum_result_t count;
    while ((count = rum_read(pipe.read_handle, buffer, sizeof buffer)) > 0) {
        for (unsigned i = 0; i < (unsigned)count; ++i) check(buffer[i] == pattern(total + i), 55);
        total += (unsigned)count;
    }
    check(!count && total == 17 && rum_close(pipe.read_handle) == 0 && collect(child, RUM_PROCESS_FAULTED) == 0, 56);
}
static void exhaust(void)
{
    struct rum_pipe_request pipe;
    for (unsigned i = 0; i < 14; ++i) pipe = create();
    pipe = (struct rum_pipe_request){ .version = 1, .size = sizeof pipe };
    check(rum_pipe(&pipe) == -RUM_EMFILE && !pipe.read_handle && !pipe.write_handle && !pipe.capacity && query(0) == 14, 57);
    /* Leave all endpoints for exit cleanup. */
}
int main(int argc, char **argv)
{
    check(argc == 2, 58);
    const char *mode = argv[1];
    if (equal(mode, "parent")) {
        invalid(); binary(false); binary(true); writers(); readers();
        for (unsigned typed = 0; typed < 2; ++typed) {
            closures(false, typed); closures(true, typed); cancellation(false, typed); cancellation(true, typed);
        }
        fault();
        check(query(0) == 0 && query(1) == 0 && query(2) == 0 && query(3) == 0, 59);
        exhaust(); return 0;
    }
    if (equal(mode, "producer")) {
        unsigned total = 0;
        while (total < PAYLOAD) {
            unsigned requested = PAYLOAD - total; if (requested > 768) requested = 768;
            for (unsigned i = 0; i < requested; ++i) buffer[i] = pattern(total + i);
            rum_result_t count = rum_write(1, buffer, requested);
            check(count > 0 && count <= (rum_result_t)RUM_PIPE_ATOMIC_WRITE, 60); total += (unsigned)count;
        }
        return 0;
    }
    if (equal(mode, "recordsA") || equal(mode, "recordsB")) {
        unsigned writer = mode[7] == 'B';
        for (unsigned record = 0; record < 16; ++record) {
            buffer[0] = (unsigned char)('A' + writer); buffer[1] = (unsigned char)record;
            for (unsigned i = 2; i < RUM_PIPE_ATOMIC_WRITE; ++i) buffer[i] = (unsigned char)(('A' + writer) ^ record ^ i);
            check(rum_write(1, buffer, RUM_PIPE_ATOMIC_WRITE) == RUM_PIPE_ATOMIC_WRITE, 61);
        }
        return 0;
    }
    if (equal(mode, "consumer")) {
        unsigned total = 0; rum_result_t count;
        while ((count = rum_read(0, buffer, 128)) > 0) {
            for (unsigned i = 0; i < (unsigned)count; ++i) check(buffer[i] == 'R', 62);
            total += (unsigned)count; check(rum_yield() == 0, 63);
        }
        check(!count, 64); return (int)total;
    }
    if (equal(mode, "read-eof")) { check(rum_read(0, buffer, 1) == 0, 65); return 0; }
    if (equal(mode, "write-epipe")) { check(rum_write(1, "x", 1) == -RUM_EPIPE, 66); return 0; }
    if (equal(mode, "typedread") || equal(mode, "typedwrite")) {
        bool write = equal(mode, "typedwrite");
        rum_object_handle_t handle = exported(write ? 1 : 0, write);
        check((write ? rum_object_write(handle, "x", 1) : rum_object_read(handle, buffer, 1)) == (write ? -RUM_EPIPE : 0), 72);
        return 0; /* Exit closes the sole table reference after the operation. */
    }
    if (equal(mode, "fault")) {
        for (unsigned i = 0; i < 17; ++i) buffer[i] = pattern(i);
        rum_object_handle_t handle = exported(1, true);
        check(rum_object_write(handle, buffer, 17) == 17, 67); __asm__ volatile("ud2");
    }
    if (equal(mode, "orphan")) {
        struct rum_pipe_request pipe = create();
        check(spawn("read-eof", pipe.read_handle, 0) > 0 && rum_close(pipe.read_handle) == 0, 68);
        waiters(1, 1); return 0;
    }
    if (equal(mode, "cancelread") || equal(mode, "cancelwrite") ||
        equal(mode, "canceltypedread") || equal(mode, "canceltypedwrite")) {
        struct rum_pipe_request pipe = create();
        if (equal(mode, "canceltypedwrite")) {
            fill(pipe.write_handle); (void)rum_object_write(exported(pipe.write_handle, true), "x", 1);
        } else if (equal(mode, "canceltypedread")) (void)rum_object_read(exported(pipe.read_handle, false), buffer, 1);
        else if (equal(mode, "cancelwrite")) { fill(pipe.write_handle); (void)rum_write(pipe.write_handle, "x", 1); }
        else (void)rum_read(pipe.read_handle, buffer, 1);
        return 69;
    }
    return 70;
}
