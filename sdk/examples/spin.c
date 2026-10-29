#include <rum/user.h>

int main(int argc, char **argv);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (;;) __asm__ volatile ("" : : : "memory");
}
