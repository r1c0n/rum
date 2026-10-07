#include <rum/user.h>

int main(int argc, char **argv);
static int write_all(const char *text, unsigned bytes)
{
    while (bytes) {
        rum_result_t n = rum_write(RUM_STDOUT, text, bytes);
        if (n <= 0 || (unsigned)n > bytes) return 0;
        text += n; bytes -= (unsigned)n;
    }
    return 1;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !write_all(argv[1], 1)) return 1;
    if (!write_all("Enter text: ", 12)) return 1;
    char line[128]; unsigned used = 0;
    for (;;) {
        char key;
        if (rum_read(RUM_STDIN, &key, 1) != 1) return 1;
        if (key == '\n') break;
        if (used < sizeof line) line[used++] = key;
    }
    if (!write_all("Child read: ", 12) || !write_all(line, used) || !write_all("\n", 1)) return 1;
    return 5;
}
