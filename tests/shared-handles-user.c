#include <rum/user.h>

#define PAGE 0x80800000u
#define TEST_CALLS 0xffffffe0u
int main(int argc, char **argv);
static struct rum_arguments arguments;
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static struct rum_arguments *args(const char *mode)
{
    arguments = (struct rum_arguments){ .argc = 2, .string_bytes = 9, .offsets = {0, 9}, .strings = "/MAP.ELF" };
    do { arguments.strings[arguments.string_bytes++] = *mode; } while (*mode++);
    return &arguments;
}
static struct rum_process_result collect(rum_result_t pid, unsigned termination)
{
    struct rum_process_result result = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof result };
    check(pid > 0 && rum_wait((rum_pid_t)pid, 0, &result) == 1 && result.process_id == (rum_pid_t)pid &&
          result.termination == termination, 90);
    if (termination == RUM_PROCESS_EXITED && result.status) rum_exit(result.status);
    return result;
}
static rum_result_t raw_duplicate(struct rum_duplicate_request *request)
{
    return rum_syscall3(RUM_SYS_DUPLICATE, (rum_address_t)(uintptr_t)request, 0, 0);
}
static rum_result_t raw_spawn(struct rum_spawn_mapped_request *request)
{
    return rum_syscall3(RUM_SYS_SPAWN_MAPPED, (rum_address_t)(uintptr_t)request, 0, 0);
}
static struct rum_object_io_request io_packet(unsigned operation, rum_object_handle_t handle)
{
    return (struct rum_object_io_request){ .version = RUM_OBJECT_IO_VERSION,
        .size = sizeof(struct rum_object_io_request), .operation = operation, .handle = handle };
}
static void typed_io(void)
{
    rum_result_t fd = rum_open("/TYPED.TXT", 3, RUM_OPEN_CREATE | RUM_OPEN_APPEND);
    rum_object_handle_t file, reduced, directory, console;
    check(fd == 3 && !rum_object_from_descriptor(3, RUM_OBJECT_FILE, RUM_RIGHT_FILE, 0, &file), 100);
    check(rum_object_write(file, "abcd", 4) == 4 && rum_write(3, "ef", 2) == 2, 101);
    uint64_t position;
    char bytes[8];
    check(!rum_object_seek(file, 0, RUM_SEEK_SET, &position) && !position &&
          rum_read(3, bytes, 2) == 2 && bytes[0] == 'a' && bytes[1] == 'b' &&
          rum_object_read(file, bytes, 2) == 2 && bytes[0] == 'c' && bytes[1] == 'd', 102);
    check(!rum_object_duplicate(file, RUM_OBJECT_FILE, RUM_RIGHT_READ, 0, &reduced) &&
          rum_object_write(reduced, "x", 1) == -RUM_EACCES && rum_object_isatty(reduced) == -RUM_EACCES, 103);
    check(!rum_close(3) && rum_remove("/TYPED.TXT") == -RUM_EBUSY && rum_replace("/TYPED.TXT", "x", 1) == -RUM_EBUSY, 104);
    rum_object_handle_t inherited;
    check(!rum_object_duplicate(file, RUM_OBJECT_FILE, RUM_RIGHT_READ, RUM_OBJECT_INHERIT, &inherited), 125);
    struct rum_arguments *child = args("typed");
    child->argc = 3; child->offsets[2] = child->string_bytes;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) child->strings[child->string_bytes++] = hex[(inherited >> (28 - i * 4)) & 15];
    child->strings[child->string_bytes++] = 0;
    (void)collect(rum_spawn("/MAP.ELF", child), RUM_PROCESS_EXITED);
    check(!rum_object_close(inherited), 126);
    check(rum_object_read(reduced, bytes, 8) == 1 && bytes[0] == 'f' &&
          rum_object_read(file, bytes, 1) == 0 && rum_object_write(file, "g", 1) == 1 && !rum_object_flush(file), 105);
    check(!rum_object_seek(file, 0, RUM_SEEK_SET, &position) && rum_object_write(file, "h", 1) == 1 &&
          !rum_object_seek(file, -2, RUM_SEEK_END, &position) && position == 6 &&
          rum_object_read(reduced, bytes, 2) == 2 && bytes[0] == 'g' && bytes[1] == 'h', 129);
    check(!rum_object_close(file) && rum_remove("/TYPED.TXT") == -RUM_EBUSY &&
          !rum_object_close(reduced) && !rum_remove("/TYPED.TXT"), 106);
    check(rum_open("/", 1, RUM_OPEN_DIRECTORY) == 3 &&
          !rum_object_from_descriptor(3, RUM_OBJECT_DIRECTORY, RUM_RIGHT_DIRECTORY, 0, &directory), 107);
    struct rum_directory_entry first = { .version = 1, .size = sizeof first }, second = first;
    struct rum_object_io_request overlap = io_packet(RUM_OBJECT_IO_READDIR, directory);
    overlap.data = (rum_address_t)(uintptr_t)&overlap;
    check(rum_object_io(&overlap) == -RUM_EINVAL, 130);
    check(rum_readdir(3, &first) == 1 && rum_object_readdir(directory, &second) == 1, 108);
    int different = 0;
    for (unsigned i = 0; i < sizeof first.name; ++i) different |= first.name[i] != second.name[i];
    check(different && !rum_close(3) && !rum_object_close(directory), 109);
    check(!rum_object_from_descriptor(1, RUM_OBJECT_CONSOLE_OUTPUT, RUM_RIGHT_OUTPUT, 0, &console) &&
          rum_object_isatty(console) == 1 && rum_object_write(console, "typed console\n", 14) == 14 &&
          rum_object_read(console, bytes, 1) == -RUM_EINVAL && !rum_object_close(console), 110);
    check(rum_open("/DATA.BIN", 1, 0) == 3 &&
          !rum_object_from_descriptor(3, RUM_OBJECT_FILE, RUM_RIGHT_COMMON | RUM_RIGHT_READ | RUM_RIGHT_SEEK, 0, &file), 111);
    rum_result_t calls = rum_syscall3(TEST_CALLS, 0, 0, 0);
    struct rum_object_io_request packet = io_packet(RUM_OBJECT_IO_READ, file);
    packet.version = 2; check(rum_object_io(&packet) == -RUM_EINVAL, 131); packet.version = 1;
    packet.size--; check(rum_object_io(&packet) == -RUM_EINVAL, 132); packet.size++;
    packet.operation = 7; check(rum_object_io(&packet) == -RUM_EINVAL, 133); packet.operation = RUM_OBJECT_IO_READ;
    packet.whence = 1; check(rum_object_io(&packet) == -RUM_EINVAL, 134); packet.whence = 0;
    packet.offset_hi = 1; check(rum_object_io(&packet) == -RUM_EINVAL, 135); packet.offset_hi = 0;
    reduced = 19;
    check(rum_object_from_descriptor(3, RUM_OBJECT_FILE, RUM_RIGHT_WRITE, 0, &reduced) == -RUM_EACCES && reduced == 19, 136);
    packet.data = PAGE + 8192 - 8; packet.bytes = 16;
    check(rum_object_io(&packet) == -RUM_EFAULT, 112);
    packet.data = PAGE + 100; packet.bytes = 1; packet.reserved[1] = 1;
    check(rum_object_io(&packet) == -RUM_EINVAL, 113); packet.reserved[1] = 0;
    packet.type = RUM_OBJECT_DIRECTORY; check(rum_object_io(&packet) == -RUM_EINVAL, 114); packet.type = 0;
    check(rum_syscall3(RUM_SYS_OBJECT_IO, (rum_address_t)(uintptr_t)&packet, 1, 0) == -RUM_EINVAL &&
          rum_object_io((void *)(PAGE + 8192 - 20)) == -RUM_EFAULT &&
          rum_syscall3(TEST_CALLS, 0, 0, 0) == calls, 115);
    struct rum_object_io_request *cross = (void *)(PAGE + 4096 - 20);
    *cross = packet;
    check(rum_object_io(cross) == 1 && *(char *)(PAGE + 100) == 'a', 116);
    check(rum_object_seek(file, -2, RUM_SEEK_CUR, &position) == -RUM_EOVERFLOW &&
          !rum_object_seek(file, 0, RUM_SEEK_CUR, &position) && position == 1, 117);
    rum_object_handle_t tokens[RUM_OBJECT_HANDLE_LIMIT - 1];
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT - 1; ++i)
        check(!rum_object_from_descriptor(3, RUM_OBJECT_FILE, RUM_RIGHT_READ, 0, &tokens[i]), 118);
    reduced = 19;
    check(rum_object_from_descriptor(3, RUM_OBJECT_FILE, RUM_RIGHT_READ, 0, &reduced) == -RUM_EMFILE && reduced == 19, 119);
    for (unsigned i = 0; i < RUM_OBJECT_HANDLE_LIMIT - 1; ++i) check(!rum_object_close(tokens[i]), 120);
    check(!rum_close(3) && !rum_object_close(file) && rum_object_read(file, bytes, 1) == -RUM_EBADF, 121);
}
static void invalid(rum_handle_t file)
{
    struct rum_handle_mapping map[2] = {{file, 0, {0}}, {file, 1, {0}}};
    struct rum_spawn_mapped_request request = { RUM_SPAWN_MAPPED_VERSION, sizeof request,
        (rum_address_t)(uintptr_t)"/MAP.ELF", (rum_address_t)(uintptr_t)args("mapped"),
        (rum_address_t)(uintptr_t)map, 2, 0, 0 };
    check(rum_open("/DOCS", 1, RUM_OPEN_DIRECTORY) == 6, 72);
    rum_result_t calls = rum_syscall3(TEST_CALLS, 0, 0, 0);
    check(rum_syscall3(RUM_SYS_DUPLICATE, UINT32_MAX, 0, 0) == -RUM_EFAULT, 10);
    check(rum_syscall3(RUM_SYS_SPAWN_MAPPED, PAGE + 3 * 4096 - 8, 0, 0) == -RUM_EFAULT, 11);
    struct rum_duplicate_request duplicate = {1, sizeof duplicate, file, RUM_DUP_ANY, 2, 0};
    check(raw_duplicate(&duplicate) == -RUM_EINVAL, 12);
    duplicate.flags = 0; duplicate.reserved = 1;
    check(raw_duplicate(&duplicate) == -RUM_EINVAL, 13);
    duplicate.reserved = 0; duplicate.source = 32;
    check(raw_duplicate(&duplicate) == -RUM_EBADF, 14);
    duplicate.source = file; duplicate.target = 32;
    check(raw_duplicate(&duplicate) == -RUM_EINVAL, 15);
    request.version = 2; check(raw_spawn(&request) == -RUM_EINVAL, 16); request.version = 1;
    request.size--; check(raw_spawn(&request) == -RUM_EINVAL, 17); request.size++;
    request.flags = 8; check(raw_spawn(&request) == -RUM_EINVAL, 18); request.flags = 0;
    request.reserved = 1; check(raw_spawn(&request) == -RUM_EINVAL, 19); request.reserved = 0;
    request.mapping_count = 33; check(raw_spawn(&request) == -RUM_EINVAL, 20);
    request.mapping_count = 0; check(raw_spawn(&request) == -RUM_EINVAL, 21); request.mapping_count = 2;
    map[1].target = 0; check(raw_spawn(&request) == -RUM_EINVAL, 22); map[1].target = 1;
    map[1].source = 32; check(raw_spawn(&request) == -RUM_EBADF, 23); map[1].source = file;
    map[1].target = 32; check(raw_spawn(&request) == -RUM_EINVAL, 24); map[1].target = 1;
    map[1].reserved[1] = 1; check(raw_spawn(&request) == -RUM_EINVAL, 25); map[1].reserved[1] = 0;
    map[0].source = 1; check(raw_spawn(&request) == -RUM_EBADF, 73); map[0].source = file;
    map[1].source = 4; check(raw_spawn(&request) == -RUM_EBADF, 74);
    map[1].source = 6; check(raw_spawn(&request) == -RUM_EISDIR, 75); map[1].source = file;
    request.mappings = PAGE + 3 * 4096 - 8;
    check(raw_spawn(&request) == -RUM_EFAULT, 26);
    request.mappings = (rum_address_t)(uintptr_t)map;
    request.path = PAGE + 3 * 4096;
    check(raw_spawn(&request) == -RUM_EFAULT, 27); request.path = (rum_address_t)(uintptr_t)"/MAP.ELF";
    request.arguments = PAGE + 2 * 4096 - 8;
    check(raw_spawn(&request) == -RUM_EFAULT, 28);
    check(rum_duplicate(file, 6, RUM_DUP_CLOSPAWN) == 6, 29);
    map[0].source = 6; request.arguments = (rum_address_t)(uintptr_t)&arguments;
    check(raw_spawn(&request) == -RUM_EACCES && rum_close(6) == 0, 30);
    check(rum_syscall3(TEST_CALLS, 0, 0, 0) == calls, 31);
}
static void parent(void)
{
    typed_io();
    char buffer[8]; uint64_t offset;
    rum_result_t file = rum_open("/DATA.BIN", 3, 0), independent = rum_open("/DATA.BIN", 1, 0);
    check(file == 3 && independent == 4 && rum_duplicate(3, RUM_DUP_ANY, 0) == 5, 32);
    invalid(3);
    struct rum_duplicate_request *cross = (void *)(PAGE + 4096 - 12);
    *cross = (struct rum_duplicate_request){1, sizeof *cross, 3, 6, 0, 0};
    check(raw_duplicate(cross) == 6 && rum_close(6) == 0, 33);
    struct rum_handle_mapping *maps = (void *)(PAGE + 4096 - 8);
    maps[0] = (struct rum_handle_mapping){3, 0, {0}};
    maps[1] = (struct rum_handle_mapping){5, 1, {0}};
    maps[2] = (struct rum_handle_mapping){4, 7, {0}};
    maps[3] = (struct rum_handle_mapping){3, 2, {0}};
    (void)collect(rum_spawn_with_handles("/MAP.ELF", args("mapped"), maps, 4, 0), RUM_PROCESS_EXITED);
    check(rum_seek(5, 0, RUM_SEEK_CUR, &offset) == 0 && offset == 6 &&
          rum_seek(4, 0, RUM_SEEK_CUR, &offset) == 0 && offset == 1, 34);
    check(rum_seek(3, 0, RUM_SEEK_SET, 0) == 0 && rum_read(5, buffer, 6) == 6 &&
          buffer[0] == 'a' && buffer[2] == 'X' && buffer[3] == 'Y' && buffer[4] == 'Z' && buffer[5] == 'W', 35);
    struct rum_handle_mapping one = {3, 8, {0}};
    check(rum_spawn_with_handles("/ABSENT.ELF", args("closed"), &one, 1, 0) == -RUM_ENOENT, 36);
    check(rum_spawn_with_handles("/DATA.BIN", &arguments, &one, 1, 0) == -RUM_ENOEXEC, 37);
    (void)collect(rum_spawn_with_handles("/MAP.ELF", args("closed"), &one, 1, RUM_SPAWN_NO_DEFAULT_STREAMS), RUM_PROCESS_EXITED);
    /* Legacy spawn and mapped spawn without mappings both get fresh streams. */
    (void)collect(rum_spawn("/MAP.ELF", args("fresh")), RUM_PROCESS_EXITED);
    (void)collect(rum_spawn_with_handles("/MAP.ELF", args("fresh"), 0, 0, 0), RUM_PROCESS_EXITED);
    struct rum_spawn_mapped_request *cross_spawn = (void *)(PAGE + 4096 - 16);
    *cross_spawn = (struct rum_spawn_mapped_request){1, sizeof *cross_spawn,
        (rum_address_t)(uintptr_t)"/MAP.ELF", (rum_address_t)(uintptr_t)args("fresh"), 0, 0, 0, 0};
    (void)collect(raw_spawn(cross_spawn), RUM_PROCESS_EXITED);
    (void)collect(rum_spawn_with_handles("/MAP.ELF", args("fault"), &one, 1, 0), RUM_PROCESS_FAULTED);
    check(rum_replace("/READY.BIN", "0", 1) == 0, 38);
    rum_result_t sleeper = rum_spawn_with_handles("/MAP.ELF", args("sleep"), &one, 1, 0);
    check(sleeper > 0, 39);
    char ready = '0';
    for (unsigned tries = 0; tries < 500 && ready != '1'; ++tries) {
        rum_result_t fd = rum_open("/READY.BIN", 1, 0);
        check(fd >= 0 && rum_read((rum_handle_t)fd, &ready, 1) == 1 && rum_close((rum_handle_t)fd) == 0, 40);
        if (ready != '1') check(rum_sleep(1) == 0, 41);
    }
    check(ready == '1', 42);
    check(rum_close(3) == 0 && rum_close(4) == 0 && rum_close(5) == 0, 43);
    check(rum_remove("/DATA.BIN") == -RUM_EBUSY && rum_replace("/DATA.BIN", "x", 1) == -RUM_EBUSY, 44);
    check(rum_terminate((rum_pid_t)sleeper, 0) == 0, 45);
    (void)collect(sleeper, RUM_PROCESS_CANCELLED);
    check(rum_replace("/DATA.BIN", "abcdef", 6) == 0 && rum_remove("/READY.BIN") == 0, 46);
    /* Exhaust descriptors through aliases and deliberately leave them for exit. */
    check(rum_open("/DATA.BIN", 1, 0) == 3, 47);
    for (unsigned i = 4; i < 32; ++i) check(rum_duplicate(3, RUM_DUP_ANY, 0) == (rum_result_t)i, 48);
    check(rum_duplicate(3, RUM_DUP_ANY, 0) == -RUM_EMFILE, 49);
}
int main(int argc, char **argv)
{
    check(argc == 2 || (argc == 3 && argv[1][0] == 't'), 1); char byte;
    if (argv[1][0] == 't') {
        rum_object_handle_t handle = 0;
        for (unsigned i = 0; i < 8; ++i) {
            unsigned digit = (unsigned char)argv[2][i];
            digit = digit <= '9' ? digit - '0' : digit - 'a' + 10;
            check(digit < 16, 127); handle = handle * 16 + digit;
        }
        check(!argv[2][8] && rum_object_read(handle, &byte, 1) == 1 && byte == 'e' &&
              rum_object_write(handle, "x", 1) == -RUM_EACCES, 128);
        return 0; /* The child owns and closes its inherited reference. */
    }
    if (argv[1][0] == 'p') { parent(); return 0; }
    if (argv[1][0] == 'o') {
        check(rum_open("/DATA.BIN", 1, 0) == 3 && rum_replace("/READY.BIN", "0", 1) == 0, 67);
        struct rum_handle_mapping map = {3, 8, {0}};
        check(rum_spawn_with_handles("/MAP.ELF", args("sleep"), &map, 1, 0) > 0, 68);
        for (;;) {
            rum_result_t fd = rum_open("/READY.BIN", 1, 0);
            check(fd >= 0 && rum_read((rum_handle_t)fd, &byte, 1) == 1 && rum_close((rum_handle_t)fd) == 0, 69);
            if (byte == '1') return 0; /* Leave a live child owning the alias. */
            check(rum_sleep(1) == 0, 70);
        }
    }
    if (argv[1][0] == 'm') {
        char bytes[2]; uint64_t position;
        check(rum_read(0, bytes, 2) == 2 && bytes[0] == 'a' && bytes[1] == 'b', 60);
        check(rum_write(1, "XY", 2) == 2 && rum_close(0) == 0 && rum_write(1, "Z", 1) == 1, 61);
        check(rum_read(7, &byte, 1) == 1 && byte == 'a' && rum_seek(7, 0, 1, &position) == 0 && position == 1, 62);
        check(rum_write(2, "W", 1) == 1, 71);
        check(rum_close(3) == -RUM_EBADF && rum_close(4) == -RUM_EBADF && rum_close(5) == -RUM_EBADF, 63);
        return 0;
    }
    if (argv[1][0] == 'c') {
        check(rum_read(0, &byte, 1) == -RUM_EBADF && rum_write(1, "x", 1) == -RUM_EBADF &&
              rum_write(2, "x", 1) == -RUM_EBADF && rum_seek(8, 0, 0, 0) == 0, 64);
        return 0;
    }
    if (argv[1][0] == 'f' && argv[1][1] == 'r') {
        rum_object_handle_t input;
        check(!rum_object_from_descriptor(0, RUM_OBJECT_CONSOLE_INPUT, RUM_RIGHT_INPUT, 0, &input) &&
              rum_object_isatty(input) == 1 && rum_object_read(input, &byte, 1) == -RUM_EACCES && !rum_object_close(input), 122);
        check(rum_read(0, &byte, 1) == -RUM_EACCES && rum_close(3) == -RUM_EBADF, 65); return 0;
    }
    if (argv[1][0] == 'b') {
        rum_object_handle_t input;
        check(!rum_object_from_descriptor(0, RUM_OBJECT_CONSOLE_INPUT, RUM_RIGHT_INPUT, 0, &input) && !rum_close(0), 123);
        (void)rum_object_read(input, &byte, 1); return 124;
    }
    if (argv[1][0] == 'f') { __asm__ volatile("ud2"); }
    check(argv[1][0] == 's' && rum_replace("/READY.BIN", "1", 1) == 0, 66);
    (void)rum_sleep(RUM_SLEEP_MAX_MS); return 99;
}
