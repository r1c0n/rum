/* Built in a separate project using the exported SDK, never kernel headers. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <rum/user.h>

static const unsigned char image_data[131073] = { [0 ... 131072] = 0x5a };
static volatile unsigned char private_bss[262145];
static void check(bool ok, int code) { if (!ok) rum_exit(code); }
static unsigned char pattern(unsigned index, unsigned seed, unsigned round)
{
    return (unsigned char)((index * 37u) ^ (index >> 7) ^ (seed * 23u) ^ (round * 11u));
}
static void private_check(unsigned seed, bool initialize)
{
    const volatile unsigned char *source = image_data;
    for (unsigned i = 0; i < sizeof image_data; ++i) check(source[i] == 0x5a, 10);
    for (unsigned i = 0; i < sizeof private_bss; ++i) {
        check(private_bss[i] == (initialize ? 0 : pattern(i, seed, 0)), 11);
        if (initialize) private_bss[i] = pattern(i, seed, 0);
    }
}
static void protected_buffer(const char *path)
{
    struct rum_vm_allocate_request request = { .version = RUM_VM_ABI_VERSION, .size = sizeof request,
        .bytes = 8192, .protection = RUM_VM_READ | RUM_VM_WRITE };
    check(!rum_vm_allocate(&request), 12);
    rum_result_t file = rum_open(path, RUM_OPEN_READ, 0); check(file >= 3, 13);
    rum_object_handle_t object;
    check(!rum_object_from_descriptor((rum_handle_t)file, RUM_OBJECT_FILE, RUM_RIGHT_READ | RUM_RIGHT_SEEK, 0, &object), 67);
    char *cross = (char *)(uintptr_t)(request.address + 4094);
    check(!rum_vm_protect(request.address, request.bytes, RUM_VM_READ), 14);
    check(rum_read((rum_handle_t)file, cross, 4) == -RUM_EFAULT, 15);
    check(rum_object_read(object, cross, 4) == -RUM_EFAULT, 68);
    uint64_t position;
    check(!rum_seek((rum_handle_t)file, 0, RUM_SEEK_CUR, &position) && !position, 16);
    check(!rum_vm_protect(request.address, request.bytes, RUM_VM_READ | RUM_VM_WRITE) &&
          !rum_close((rum_handle_t)file) && rum_object_read(object, cross, 4) == 4 && !rum_object_close(object) &&
          !rum_vm_release(request.address, request.bytes), 17);
}
static void file_work(unsigned seed, unsigned round, char mode)
{
    unsigned char *data = calloc(65537, 1), *guard = malloc(8193);
    check(data && guard, 20);
    for (unsigned i = 0; i < 65537; ++i) check(!data[i], 21);
    memset(guard, (int)seed, 8193);
    for (unsigned i = 0; i < 8193; ++i) data[i] = pattern(i, seed, round);
    unsigned char *grown = realloc(data, 262145); check(grown != NULL, 22); data = grown;
    check(!realloc(data, SIZE_MAX) && errno == ENOMEM && !calloc(SIZE_MAX, 2), 23);
    for (unsigned i = 0; i < 8193; ++i) check(data[i] == pattern(i, seed, round) && guard[i] == seed, 24);
    char path[16]; check(snprintf(path, sizeof path, "/S%u.BIN", seed) > 0, 25);
    FILE *file = fopen(path, "w+b"); check(file != NULL, 26);
    check(fwrite(data, 1, 8193, file) == 8193 && !fflush(file) && !fseek(file, 0, SEEK_SET), 27);
    check(!rum_sleep(1), 28); /* Keep a real FAT reference while other processes run. */
    unsigned char copy[511]; unsigned offset = 0; size_t bytes;
    while ((bytes = fread(copy, 1, sizeof copy, file)) != 0) {
        check(offset <= 8193 && bytes <= 8193 - offset && !memcmp(copy, data + offset, bytes), 29);
        offset += bytes;
    }
    check(offset == 8193 && feof(file) && !ferror(file), 30);
    rum_result_t descriptor = rum_open(path, RUM_OPEN_READ | RUM_OPEN_WRITE, 0);
    rum_object_handle_t object;
    check(descriptor >= 3 && !rum_object_from_descriptor((rum_handle_t)descriptor, RUM_OBJECT_FILE, RUM_RIGHT_FILE, 0, &object) &&
          !rum_close((rum_handle_t)descriptor), 69);
    /* Rewrite identical bytes through the typed path; the host audits all
       8193 bytes after guest exit. Short transfers keep the same 512-byte cap. */
    check(rum_object_write(object, data, 513) == 512 && rum_object_write(object, data + 512, 1) == 1, 70);
    uint64_t position;
    check(!rum_object_seek(object, 0, RUM_SEEK_END, &position) && position == 8193 && !rum_object_flush(object), 71);
    check(rum_remove(path) == -RUM_EBUSY && rum_replace(path, data, 1) == -RUM_EBUSY, 72);
    if (mode == 'f') __asm__ volatile("ud2");
    if (mode == 'g') *(volatile unsigned char *)(uintptr_t)(RUM_ABI_STACK_BASE - 1) = 0;
    if (mode == 'r') *(volatile unsigned char *)(uintptr_t)image_data = 0;
    if (mode == 'c') {
        check(rum_write(3, "R", 1) == 1, 31);
        for (;;) __asm__ volatile("pause");
    }
    check(!fclose(file) && !rum_object_close(object), 32); protected_buffer(path);
    free(guard); free(data); private_check(seed, false);
}
static void pressure(bool exhaust)
{
    unsigned char *chunks[24]; unsigned count = 0;
    for (; count < (exhaust ? 24u : 2u); ++count) {
        chunks[count] = malloc(1024 * 1024);
        if (!chunks[count]) break;
        chunks[count][0] = (unsigned char)count;
        chunks[count][1024 * 1024 - 1] = (unsigned char)(count ^ 0x7f);
    }
    check(count && (!exhaust || (count < 24 && errno == ENOMEM)), 40);
    check(!rum_sleep(30), 41);
    for (unsigned i = 0; i < count; ++i) {
        check(chunks[i][0] == i && chunks[i][1024 * 1024 - 1] == (i ^ 0x7f), 42);
        free(chunks[i]);
    }
    void *again = calloc(8193, 1); check(again != NULL, 43); free(again);
}
static rum_pid_t launch(const char *self, char mode, unsigned seed, rum_handle_t control)
{
    struct rum_arguments args = { .argc = 3 };
    size_t length = strlen(self); check(length < 256, 50);
    memcpy(args.strings, self, length + 1);
    args.offsets[1] = (uint32_t)length + 1; args.strings[length + 1] = mode;
    args.offsets[2] = (uint32_t)length + 3; args.strings[length + 3] = (char)('0' + seed);
    args.string_bytes = (uint32_t)length + 5;
    struct rum_handle_mapping map = { .source = control, .target = 3 };
    rum_result_t pid = mode == 'c' ? rum_spawn_with_handles(self, &args, &map, 1, 0) : rum_spawn(self, &args);
    check(pid > 0, 51); return (rum_pid_t)pid;
}
static void collect(rum_pid_t pid, unsigned termination, unsigned vector)
{
    struct rum_process_result result = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof result };
    check(rum_wait(pid, 0, &result) == 1 && result.termination == termination &&
          (termination == RUM_PROCESS_EXITED ? result.status == 0 : result.fault_vector == vector), 52);
}
int main(int argc, char **argv)
{
    struct rum_object_information object;
    check(rum_object_query(RUM_OBJECT_INVALID_HANDLE, RUM_OBJECT_ANY, &object) == -RUM_EBADF, 66);
    unsigned seed = argc == 3 ? (unsigned)(argv[2][0] - '0') : 0;
    private_check(seed, true);
    if (argc == 3) {
        char mode = argv[1][0];
        if (mode == 'p' || mode == 'e') pressure(mode == 'e');
        else for (unsigned round = 0; round < 4; ++round) file_work(seed, round, mode);
        return 0;
    }
    check(argc == 1, 60);
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        rum_pid_t children[4];
        for (unsigned i = 0; i < 3; ++i) children[i] = launch(argv[0], 'w', i + 1, 0);
        children[3] = launch(argv[0], 'p', 0, 0);
        for (unsigned i = 0; i < 4; ++i) collect(children[i], RUM_PROCESS_EXITED, 0);
        collect(launch(argv[0], 'e', 0, 0), RUM_PROCESS_EXITED, 0);
        collect(launch(argv[0], 'f', 4, 0), RUM_PROCESS_FAULTED, 6);
        collect(launch(argv[0], 'g', 4, 0), RUM_PROCESS_FAULTED, 14);
        collect(launch(argv[0], 'r', 4, 0), RUM_PROCESS_FAULTED, 14);
        struct rum_pipe_request pipe = { .version = RUM_PIPE_ABI_VERSION, .size = sizeof pipe };
        check(!rum_pipe(&pipe), 61);
        rum_pid_t child = launch(argv[0], 'c', 5, pipe.write_handle);
        check(!rum_close(pipe.write_handle), 62);
        char ready; check(rum_read(pipe.read_handle, &ready, 1) == 1 && ready == 'R', 63);
        check(!rum_terminate(child, 0), 64); collect(child, RUM_PROCESS_CANCELLED, 0);
        check(rum_read(pipe.read_handle, &ready, 1) == 0 && !rum_close(pipe.read_handle), 65);
        private_check(0, false);
    }
    return puts("SDK mixed memory and I/O passed.") == EOF;
}
