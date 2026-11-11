#include <rum/user.h>
#include <stdio.h>
#include <string.h>
#define TEST_WRITES 0xffffffe0u
static void check(int ok, int status) { if (!ok) rum_exit(status); }
static uint32_t token_number(const char *text)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned digit = (unsigned char)text[i]; digit = digit <= '9' ? digit - '0' : digit - 'a' + 10;
        check(digit < 16, 1); value = value * 16 + digit;
    }
    check(!text[8], 2); return value;
}
int main(int argc, char **argv)
{
    check(argc == 3, 3);
    if (argv[1][0] == 'i') { __asm__ volatile("outb %0, $0x80" : : "a"((unsigned char)0)); return 4; }
    rum_object_handle_t original = token_number(argv[2]);
    if (argv[1][0] == 'c') {
        struct rum_object_information info;
        check(!rum_object_query(original, RUM_OBJECT_FILE, &info) && info.rights == (RUM_RIGHT_QUERY | RUM_RIGHT_READ) &&
              rum_object_write(original, "bad", 3) == -RUM_EACCES &&
              rum_session(RUM_SESSION_PIPELINE_BEGIN, NULL) == -RUM_EACCES &&
              rum_replace("/RUM/CORE.BIN", "bad", 3) == -RUM_EACCES, 5);
        return 0;
    }
    rum_result_t before = rum_syscall3(TEST_WRITES, 0, 0, 0);
    if (before >= 0) check(!rum_syscall3(TEST_WRITES, 1, 0, 0), 6);
    const char *paths[] = {"/rum/core.bin", "/RUM//./CORE.BIN", "/DOCS/../RUM/SUB/../CORE.BIN", "/SYS/CORE.BIN", "/CORE.ALS"};
    for (unsigned i = 0; i < (before < 0 ? 3u : 5u); ++i) {
        check(rum_open(paths[i], RUM_OPEN_WRITE, RUM_OPEN_CREATE | RUM_OPEN_TRUNCATE) == -RUM_EACCES &&
              rum_replace(paths[i], "bad", 3) == -RUM_EACCES && rum_remove(paths[i]) == -RUM_EACCES, 10 + (int)i);
    }
    check(rum_mkdir("/RUM/NEW") == -RUM_EACCES && rum_remove("/RUM") == -RUM_EACCES &&
          rum_rename("/RUM", "/MOVED", 0) == -RUM_EACCES &&
          rum_rename("/RUM/CORE.BIN", "/OUT.TXT", 0) == -RUM_EACCES &&
          rum_rename("/APPS/OUT.TXT", "/RUM/CORE.BIN", RUM_RENAME_REPLACE) == -RUM_EACCES, 16);
    check(!rum_chdir("/RUM/SUB") && rum_replace("../CORE.BIN", "bad", 3) == -RUM_EACCES && !rum_chdir("/"), 17);
    check(rum_open("/RUM/BAD?", RUM_OPEN_WRITE, RUM_OPEN_CREATE) == -RUM_EINVAL, 18);
    rum_result_t input = rum_open("/RUM/CORE.BIN", RUM_OPEN_READ, 0); char bytes[16];
    check(input >= 3 && rum_read((rum_handle_t)input, bytes, 7) == 7 && !memcmp(bytes, "trusted", 7) && !rum_close((rum_handle_t)input), 19);
    rum_result_t role = rum_session(RUM_SESSION_PIPELINE_BEGIN, NULL);
    if (argv[1][0] == 's') check(!role && !rum_session(RUM_SESSION_PIPELINE_END, NULL), 20);
    else check(role == -RUM_EACCES, 21);
    rum_object_handle_t no_rights = 19;
    check(rum_process_open((rum_pid_t)rum_getpid(), RUM_RIGHT_TERMINATE, 0, &no_rights) == -RUM_EACCES &&
          rum_terminate((rum_pid_t)rum_getpid(), 0) == -RUM_EACCES, 22);
    struct rum_spawn_request forged = { .version = 1, .size = sizeof forged, .flags = UINT32_MAX };
    check(rum_syscall3(RUM_SYS_SPAWN, (rum_address_t)(uintptr_t)&forged, 0, 0) == -RUM_EINVAL &&
          rum_syscall3(RUM_SYS_COUNT + 1, 0, 0, 0) == -RUM_ENOSYS, 23);
    if (original) {
        uint64_t position;
        check(rum_write(8, "bad", 3) == -RUM_EACCES && rum_write(8, NULL, 0) == -RUM_EACCES &&
              !rum_seek(8, 0, RUM_SEEK_CUR, &position) && !position && rum_object_write(original, "bad", 3) == -RUM_EACCES, 24);
        check(rum_duplicate(8, RUM_DUP_ANY, 0) == -RUM_EACCES &&
              rum_object_duplicate(original, RUM_OBJECT_FILE, RUM_RIGHT_WRITE, 0, &no_rights) == -RUM_EACCES &&
              rum_object_from_descriptor(8, RUM_OBJECT_FILE, RUM_RIGHT_WRITE, 0, &no_rights) == -RUM_EACCES, 25);
        struct rum_arguments args = { .argc = 3, .offsets = {0, 14, 20}, .string_bytes = 29,
            .strings = "/RUM/AUTH.ELF\0child\0" "00000000" };
        memcpy(args.strings + 20, argv[2], 9);
        check(rum_spawn("/RUM/AUTH.ELF", &args) == -RUM_EACCES, 26);
        struct rum_handle_mapping map = {8, 0, {0}};
        check(rum_spawn_with_objects("/RUM/AUTH.ELF", &args, &map, 1, 0, NULL, 0) == -RUM_EACCES, 27);
        struct rum_object_inheritance grant = { .handle = original, .type = RUM_OBJECT_FILE, .rights = RUM_RIGHT_QUERY | RUM_RIGHT_READ };
        rum_result_t child = rum_spawn_with_objects("/RUM/AUTH.ELF", &args, NULL, 0, 0, &grant, 1);
        struct rum_process_result status = { .version = 1, .size = sizeof status };
        check(child > 0 && rum_wait((rum_pid_t)child, 0, &status) == 1 && status.termination == RUM_PROCESS_EXITED && !status.status, 28);
        check(!rum_object_close(original) && !rum_close(8), 29);
    }
    if (before >= 0) check(rum_syscall3(TEST_WRITES, 0, 0, 0) == before, 30);
    rum_result_t made = rum_mkdir("/APPS"); check(!made || made == -RUM_EEXIST, 31);
    check(!rum_replace("/APPS/OUT.TXT", "ordinary", 8), 32);
    rum_result_t file = rum_open("/APPS/OUT.TXT", RUM_OPEN_READ | RUM_OPEN_WRITE, 0);
    check(file >= 3 && rum_write((rum_handle_t)file, "O", 1) == 1 && !rum_close((rum_handle_t)file), 33);
    check(!rum_rename("/APPS/OUT.TXT", "/APPS/KEEP.TXT", RUM_RENAME_REPLACE), 34);
    check(!rum_replace("/RUM1.TXT", "ok", 2) && !rum_remove("/RUM1.TXT"), 35);
    return puts("System authority checks passed.") == EOF;
}
