#include <rum/user.h>
#include <stdio.h>
#include <string.h>

static rum_object_handle_t handles[3];
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static uint32_t number(const char *text)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned digit = (unsigned char)text[i];
        check(digit >= '0' && digit <= 'f', 70);
        digit = digit <= '9' ? digit - '0' : digit - 'a' + 10;
        check(digit < 16, 71); result = result * 16 + digit;
    }
    check(!text[8], 72); return result;
}
static struct rum_object_request packet(unsigned action, rum_object_handle_t handle)
{
    return (struct rum_object_request){ .version = RUM_OBJECT_ABI_VERSION, .size = sizeof(struct rum_object_request),
        .operation = action, .handle = handle };
}
static void invalid(void)
{
    struct rum_object_information info;
    check(rum_object_query(0, RUM_OBJECT_ANY, &info) == -RUM_EBADF &&
          rum_object_close(RUM_STDOUT) == -RUM_EBADF, 10);
    check(rum_object_query(handles[0], RUM_OBJECT_PROCESS, &info) == -RUM_EINVAL &&
          rum_object_query(handles[0] ^ 0x20u, RUM_OBJECT_ANY, &info) == -RUM_EBADF, 11);
    char byte;
    check(rum_read(handles[0], &byte, 1) == -RUM_EBADF && rum_close(handles[0]) == -RUM_EBADF &&
          rum_duplicate(handles[0], RUM_DUP_ANY, 0) == -RUM_EBADF, 12);
    struct rum_object_request request = packet(RUM_OBJECT_QUERY, handles[0]);
    request.version++; check(rum_object_control(&request) == -RUM_EINVAL, 13);
    request = packet(RUM_OBJECT_QUERY, handles[0]); request.size--;
    check(rum_object_control(&request) == -RUM_EINVAL, 14);
    request = packet(RUM_OBJECT_QUERY, handles[0]); request.reserved[2] = 1;
    check(rum_object_control(&request) == -RUM_EINVAL, 15);
    request = packet(RUM_OBJECT_QUERY, handles[0]); request.result_handle = 1;
    check(rum_object_control(&request) == -RUM_EINVAL, 16);
    request = packet(RUM_OBJECT_DUPLICATE, handles[0]); request.rights = 0x80000000u;
    check(rum_object_control(&request) == -RUM_EINVAL, 17);
    request.rights = RUM_RIGHT_TERMINATE; check(rum_object_control(&request) == -RUM_EINVAL, 18);
    request.rights = RUM_RIGHT_WRITE; check(rum_object_control(&request) == -RUM_EACCES, 19);
    request.flags = 2; check(rum_object_control(&request) == -RUM_EINVAL, 20);
    request = packet(RUM_OBJECT_QUERY, handles[0]);
    check(rum_syscall3(RUM_SYS_OBJECT, (rum_address_t)(uintptr_t)&request, 1, 0) == -RUM_EINVAL &&
          rum_syscall3(RUM_SYS_OBJECT, UINT32_MAX - 20, 0, 0) == -RUM_EFAULT &&
          rum_object_control(NULL) == -RUM_EFAULT, 21);
    struct rum_vm_allocate_request regions[3];
    for (unsigned i = 0; i < 3; ++i) {
        regions[i] = (struct rum_vm_allocate_request){ .version = RUM_VM_ABI_VERSION, .size = sizeof regions[i],
            .address = 0x90000000u + 4096 * i, .bytes = 4096, .protection = RUM_VM_READ | RUM_VM_WRITE };
        check(!rum_vm_allocate(&regions[i]), 22);
    }
    struct rum_object_request *cross = (void *)(uintptr_t)(regions[0].address + 4096 - 20);
    *cross = packet(RUM_OBJECT_QUERY, handles[0]);
    check(!rum_object_control(cross) && cross->type == RUM_OBJECT_FILE, 23);
    *cross = packet(RUM_OBJECT_DUPLICATE, handles[0]); cross->rights = RUM_RIGHT_QUERY;
    struct rum_object_request saved = *cross;
    check(!rum_vm_protect(regions[1].address, 4096, RUM_VM_READ), 24);
    check(rum_object_control(cross) == -RUM_EFAULT && !memcmp(cross, &saved, sizeof saved), 25);
    check(!rum_vm_protect(regions[1].address, 4096, RUM_VM_READ | RUM_VM_WRITE), 26);
    unsigned char *tail = (void *)(uintptr_t)(regions[2].address + 4096 - 20);
    memset(tail, 0x5a, 20);
    check(rum_object_control((void *)tail) == -RUM_EFAULT, 27);
    for (unsigned i = 0; i < 20; ++i) check(tail[i] == 0x5a, 28);
    for (unsigned i = 0; i < 3; ++i) check(!rum_vm_release(regions[i].address, regions[i].bytes), 29);
}
static void aliases(void)
{
    struct rum_object_information info;
    check(!rum_object_query(handles[0], RUM_OBJECT_FILE, &info) && info.version == RUM_OBJECT_ABI_VERSION &&
          info.size == sizeof info && info.rights == (RUM_RIGHT_COMMON | RUM_RIGHT_READ), 30);
    rum_object_handle_t reduced = 37, reused = 0;
    check(!rum_object_duplicate(handles[0], RUM_OBJECT_FILE, RUM_RIGHT_QUERY, 0, &reduced), 31);
    check(!rum_object_query(reduced, RUM_OBJECT_FILE, &info) && info.rights == RUM_RIGHT_QUERY, 32);
    reused = 37;
    check(rum_object_duplicate(reduced, RUM_OBJECT_FILE, RUM_RIGHT_READ, 0, &reused) == -RUM_EACCES && reused == 37, 33);
    check(!rum_object_close(reduced), 34);
    check(!rum_object_duplicate(handles[0], RUM_OBJECT_FILE, 0, 0, &reused) && reused != reduced, 35);
    check(rum_object_close(reduced) == -RUM_EBADF &&
          rum_object_query(reused, RUM_OBJECT_FILE, &info) == -RUM_EACCES && !rum_object_close(reused), 36);
    rum_object_handle_t extra[RUM_OBJECT_HANDLE_LIMIT]; unsigned count = 0;
    for (; count < RUM_OBJECT_HANDLE_LIMIT - 3; ++count)
        check(!rum_object_duplicate(handles[0], RUM_OBJECT_ANY, RUM_RIGHT_QUERY, 0, &extra[count]), 37);
    reused = 37;
    check(rum_object_duplicate(handles[0], RUM_OBJECT_ANY, 0, 0, &reused) == -RUM_EMFILE && reused == 37, 38);
    for (unsigned i = 0; i < count; ++i) check(!rum_object_close(extra[i]), 39);
    /* Reusable stream slots and shared offsets retain their existing ABI. */
    rum_result_t file = rum_open("/ROOT.TXT", RUM_OPEN_READ, 0);
    check(file == 3 && rum_duplicate(3, RUM_DUP_ANY, RUM_DUP_CLOSPAWN) == 4, 40);
    char byte;
    check(rum_read(3, &byte, 1) == 1 && byte == 'r' && rum_read(4, &byte, 1) == 1 && byte == 'o' &&
          !rum_close(3) && !rum_close(4), 41);
}
static void inherit(int argc, char **argv)
{
    struct rum_arguments args = { .argc = 6 };
    rum_object_handle_t reduced;
    check(!rum_object_duplicate(handles[0], RUM_OBJECT_FILE, RUM_RIGHT_QUERY, RUM_OBJECT_INHERIT, &reduced), 42);
    char hex[9]; check(snprintf(hex, sizeof hex, "%08x", reduced) == 8, 43);
    const char *values[] = {"/CAP.ELF", "inherit", argv[2], argv[3], argv[4], hex};
    (void)argc;
    for (unsigned i = 0; i < 6; ++i) {
        args.offsets[i] = args.string_bytes;
        size_t bytes = strlen(values[i]) + 1;
        memcpy(args.strings + args.string_bytes, values[i], bytes); args.string_bytes += bytes;
    }
    rum_result_t pid = rum_spawn("/CAP.ELF", &args);
    struct rum_process_result result = { .version = RUM_PROCESS_ABI_VERSION, .size = sizeof result };
    check(pid > 0 && rum_wait((rum_pid_t)pid, 0, &result) == 1 && result.termination == RUM_PROCESS_EXITED && result.status == 17, 44);
    struct rum_object_information info;
    check(!rum_object_query(handles[0], RUM_OBJECT_FILE, &info) && !rum_object_close(reduced), 45);
}
int main(int argc, char **argv)
{
    check(argc >= 5, 73);
    for (unsigned i = 0; i < 3; ++i) handles[i] = number(argv[i + 2]);
    struct rum_object_information info;
    if (!strcmp(argv[1], "wrap")) {
        check(!rum_object_query(handles[0], RUM_OBJECT_FILE, &info), 46);
        rum_object_handle_t unused = 19;
        check(rum_object_duplicate(handles[0], RUM_OBJECT_ANY, 0, 0, &unused) == -RUM_EMFILE && unused == 19 &&
              !rum_object_close(handles[0]) && rum_object_close(handles[0]) == -RUM_EBADF, 47);
        return 0;
    }
    if (!strcmp(argv[1], "inherit")) {
        check(argc == 6 && !rum_object_query(handles[0], RUM_OBJECT_FILE, &info) &&
              info.rights == (RUM_RIGHT_COMMON | RUM_RIGHT_READ), 48);
        check(rum_object_query(handles[1], RUM_OBJECT_ANY, &info) == -RUM_EBADF &&
              rum_object_query(handles[2], RUM_OBJECT_ANY, &info) == -RUM_EBADF, 49);
        rum_object_handle_t reduced = number(argv[5]), duplicate = 0;
        check(!rum_object_query(reduced, RUM_OBJECT_FILE, &info) && info.rights == RUM_RIGHT_QUERY &&
              rum_object_duplicate(reduced, RUM_OBJECT_FILE, RUM_RIGHT_WRITE, 0, &duplicate) == -RUM_EACCES, 50);
        check(!rum_object_close(handles[0]), 51); return 17;
    }
    if (!strcmp(argv[1], "fault") || !strcmp(argv[1], "spin")) {
        rum_object_handle_t alias;
        check(!rum_object_duplicate(handles[0], RUM_OBJECT_FILE, RUM_RIGHT_QUERY, 0, &alias), 52);
        if (argv[1][0] == 'f') __asm__ volatile("ud2");
        puts("typed handle cancellation ready");
        for (;;) __asm__ volatile("pause");
    }
    invalid(); aliases(); inherit(argc, argv);
    puts("Typed handle ABI passed."); return 0; /* Leave the three original entries for teardown. */
}
