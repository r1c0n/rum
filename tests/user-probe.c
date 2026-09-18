/* Separate user executable; uses the same startup/runtime as hello. */
#include <rum/user.h>
#include <rum/abi/layout.h>

int user_test_main(int argc, char **argv);
int user_probe_registers(void);
volatile uint32_t user_entry_alignment, user_entry_flags, user_entry_ebp;
static volatile uint32_t initialized = 0x1234ABCD;
static volatile unsigned char zeroes[4096];
static char input[16];

int user_test_main(int argc, char **argv)
{
    if (user_entry_alignment != 12 || (user_entry_flags & 0x3600) != 0x200 ||
        user_entry_ebp || initialized != 0x1234ABCD) return 1;
    for (unsigned i = 0; i < sizeof(zeroes); ++i) if (zeroes[i]) return 2;
    if ((argc != 2 && argc != RUM_ABI_ARGUMENT_LIMIT) || argv[argc] || argv[argc + 1]) return 3;
    unsigned bytes = 0;
    for (int i = 0; i < argc; ++i) {
        unsigned length = i == 0 ? 10 : argc == 2 ? 4 :
                          i == argc - 1 ? RUM_ABI_ARGUMENT_BYTES - 10 - 30 * 128 : 128;
        uintptr_t address = (uintptr_t)argv[i];
        if (address < RUM_ABI_STACK_BASE || address > RUM_ABI_STACK_TOP - length) return 4;
        for (unsigned j = 0; j < length; ++j) {
            char expected = j == length - 1 ? 0 : i == 0 ? "abi-probe"[j] :
                            argc == 2 ? "rum"[j] : (char)('A' + i % 26);
            if (argv[i][j] != expected) return 5;
        }
        bytes += length;
    }
    if (bytes != (argc == 2 ? 14u : RUM_ABI_ARGUMENT_BYTES) || !user_probe_registers()) return 6;
    if (rum_getpid() != 42 || rum_read(RUM_STDOUT, input, sizeof(input)) != -RUM_EBADF) return 7;
    if (rum_read(RUM_STDIN, input, sizeof(input)) != 3 || input[0] != 'r' ||
        input[1] != 'u' || input[2] != 'm' || input[3]) return 8;
    if (rum_write(RUM_STDOUT, input, 3) != 3 || rum_syscall3(99, 0, 0, 0) != -RUM_ENOSYS) return 9;
    /* Fixture-only clock query; not part of rum's public syscall table. */
    while (rum_syscall3(0xFFFFFFF0, 0, 0, 0) < 3) { }
    return -37; /* Prove main's full signed result reaches exit. */
}
