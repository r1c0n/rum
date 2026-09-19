#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <rum/gdt.h>
#include <rum/cpu_policy.h>
#include <rum/interrupts.h>

int main(void)
{
    size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    char *pages = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pages != MAP_FAILED);
    assert(mprotect(pages + page_size, page_size, PROT_NONE) == 0);
    /* Place the short frame against an inaccessible page: reading a user
       tail from a kernel interrupt must fail this test, even accidentally. */
    struct exception_frame *kernel = (void *)(pages + page_size - sizeof *kernel);
    *kernel = (struct exception_frame){ .cs = KERNEL_CODE_SELECTOR, .saved_esp = 0x12340000 };
    assert(!exception_frame_from_user(kernel));
    assert(exception_frame_esp(kernel) == 0x12340014);
    assert(exception_frame_ss(kernel) == KERNEL_DATA_SELECTOR);
    struct exception_user_frame user = {
        .core = { .cs = USER_CODE_SELECTOR, .saved_esp = 0xDEADBEEF },
        .esp = 0xBFFFFFF0, .ss = USER_DATA_SELECTOR,
    };
    assert(exception_frame_from_user(&user.core));
    assert(exception_frame_esp(&user.core) == user.esp);
    assert(exception_frame_ss(&user.core) == USER_DATA_SELECTOR);
    /* Garbage resembling privileged kernel flags/registers must disappear. */
    memset(&user, 0xFF, sizeof user);
    cpu_user_frame_initialize(&user, 0x80001000, 0xBFFFFFF0);
    assert(user.core.eflags == 0x202);
    assert(user.core.eax == 0 && user.core.ebx == 0 && user.core.saved_esp == 0);
    assert(user.core.ecx == 0 && user.core.edx == 0 && user.core.esi == 0 &&
           user.core.edi == 0 && user.core.ebp == 0 && user.core.vector == 0 && user.core.error == 0);
    assert(user.core.cs == USER_CODE_SELECTOR && user.ss == USER_DATA_SELECTOR);
    assert(user.core.ds == USER_DATA_SELECTOR && user.core.es == USER_DATA_SELECTOR);
    assert(user.core.fs == USER_DATA_SELECTOR && user.core.gs == USER_DATA_SELECTOR);
    assert(user.core.eip == 0x80001000 && user.esp == 0xBFFFFFF0);
    /* The longer privilege-change frame must fit exactly too. Helpers mask
       selector padding instead of exposing undefined upper bits. */
    struct exception_user_frame *boundary = (void *)(pages + page_size - sizeof *boundary);
    *boundary = user;
    boundary->core.cs |= 0xABCD0000;
    boundary->ss |= 0x98760000;
    assert(exception_frame_from_user(&boundary->core));
    assert(exception_frame_esp(&boundary->core) == user.esp);
    assert(exception_frame_ss(&boundary->core) == USER_DATA_SELECTOR);
    assert(munmap(pages, page_size * 2) == 0);
    puts("PASS: interrupt frame lengths and privilege-change stack access");
    return 0;
}
