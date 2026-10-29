#include <rum/user.h>

int main(int argc, char **argv);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static const char message[] = "Hello from rum userspace!\n";
    rum_size_t written = 0;
    while (written < sizeof(message) - 1) {
        rum_result_t result = rum_write(RUM_STDOUT, message + written,
                                       sizeof(message) - 1 - written);
        if (result <= 0 || (rum_size_t)result > sizeof(message) - 1 - written) return 1;
        written += (rum_size_t)result;
    }
    return 0;
}
