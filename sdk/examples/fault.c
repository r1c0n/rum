#include <rum/user.h>

int main(int argc, char **argv);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    __asm__ volatile ("ud2");
    return 0;
}
