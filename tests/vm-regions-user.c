#include <stdbool.h>
#include <rum/user.h>

#define REPORT 0xffffffe0u
#define PEERS  0xffffffe1u
#define PIPE_WRITER 0xffffffe2u
#define PAGE RUM_ABI_PAGE_SIZE
static rum_address_t addresses[RUM_VM_REGION_LIMIT];
rum_result_t vm_release_on_stack(rum_address_t top, const struct rum_vm_release_request *);
rum_result_t vm_protect_on_stack(rum_address_t top, const struct rum_vm_protect_request *);
static const struct rum_vm_allocate_request readonly_packet = {
    .version = RUM_VM_ABI_VERSION, .size = sizeof readonly_packet,
    .bytes = PAGE, .protection = RUM_VM_READ | RUM_VM_WRITE };
static const struct rum_vm_protect_request readonly_protect = {
    .version = RUM_VM_ABI_VERSION, .size = sizeof readonly_protect,
    .address = RUM_ABI_STACK_BASE, .bytes = RUM_ABI_STACK_SIZE, .protection = RUM_VM_READ };

static void check(bool ok, unsigned code)
{
    if (!ok) rum_exit((int)code);
}
static struct rum_vm_allocate_request packet(uint32_t bytes)
{
    return (struct rum_vm_allocate_request){ .version = RUM_VM_ABI_VERSION,
        .size = sizeof(struct rum_vm_allocate_request), .bytes = bytes,
        .protection = RUM_VM_READ | RUM_VM_WRITE };
}
static rum_result_t raw(unsigned call, const void *request)
{
    return rum_syscall3(call, (rum_address_t)(uintptr_t)request, 0, 0);
}
static void invalid(void)
{
    check(rum_vm_allocate(NULL) == -RUM_EFAULT &&
          raw(RUM_SYS_VM_ALLOCATE, &readonly_packet) == -RUM_EFAULT &&
          raw(RUM_SYS_VM_RELEASE, (void *)0xfffffff0u) == -RUM_EFAULT, 1);
    struct rum_vm_allocate_request request = packet(PAGE);
    request.version++;
    check(rum_vm_allocate(&request) == -RUM_EINVAL, 2);
    request = packet(UINT32_MAX);
    check(rum_vm_allocate(&request) == -RUM_EOVERFLOW && request.bytes == UINT32_MAX && !request.address, 3);
    request = packet(0);
    check(rum_vm_allocate(&request) == -RUM_EINVAL, 4);
    request = packet(PAGE); request.address = RUM_ABI_STACK_BASE;
    check(rum_vm_allocate(&request) == -RUM_EINVAL, 5);
    request.address = (rum_address_t)(uintptr_t)&readonly_packet & ~(PAGE - 1);
    check(rum_vm_allocate(&request) == -RUM_EEXIST, 6);
    check(rum_vm_release(RUM_ABI_STACK_BASE, RUM_ABI_STACK_SIZE) == -RUM_EACCES &&
          rum_vm_release(request.address, PAGE) == -RUM_EACCES, 7);
    request = packet(PAGE); request.flags = 1;
    check(rum_vm_allocate(&request) == -RUM_EINVAL, 8);
    request.flags = 0; request.reserved[1] = 1;
    check(rum_vm_allocate(&request) == -RUM_EINVAL, 9);
    request.reserved[1] = 0;
    check(rum_syscall3(RUM_SYS_VM_ALLOCATE, (rum_address_t)(uintptr_t)&request, 1, 0) == -RUM_EINVAL, 10);
    struct rum_vm_release_request bad = { .version = RUM_VM_ABI_VERSION, .size = sizeof bad,
        .address = RUM_ABI_STACK_BASE, .bytes = RUM_ABI_STACK_SIZE, .flags = 1 };
    check(raw(RUM_SYS_VM_RELEASE, &bad) == -RUM_EINVAL, 17);
    bad.flags = 0; bad.reserved[2] = 1;
    check(raw(RUM_SYS_VM_RELEASE, &bad) == -RUM_EINVAL, 18);
    bad.reserved[2] = 0;
    check(rum_syscall3(RUM_SYS_VM_RELEASE, (rum_address_t)(uintptr_t)&bad, 0, 1) == -RUM_EINVAL, 19);

    struct rum_vm_allocate_request control = packet(2 * PAGE);
    check(rum_vm_allocate(&control) == 0, 11);
    check(raw(RUM_SYS_VM_ALLOCATE, (void *)(uintptr_t)(control.address + 2 * PAGE - 16)) == -RUM_EFAULT, 28);
    struct rum_vm_allocate_request *cross = (void *)(uintptr_t)(control.address + PAGE - 16);
    *cross = packet(PAGE + 1);
    check(rum_vm_allocate(cross) == 0 && cross->bytes == 2 * PAGE &&
          rum_vm_release(cross->address, PAGE) == -RUM_EINVAL &&
          rum_vm_release(cross->address, cross->bytes) == 0 &&
          rum_vm_release(cross->address, cross->bytes) == -RUM_ENOENT, 12);
    struct rum_vm_release_request *self = (void *)cross;
    *self = (struct rum_vm_release_request){ .version = RUM_VM_ABI_VERSION, .size = sizeof *self,
        .address = control.address, .bytes = control.bytes };
    check(raw(RUM_SYS_VM_RELEASE, self) == 0, 13); /* Never touch self again. */
    check(rum_vm_release(control.address, control.bytes) == -RUM_ENOENT, 14);
    request = packet(2 * PAGE);
    check(rum_vm_allocate(&request) == 0, 38);
    bad = (struct rum_vm_release_request){ .version = RUM_VM_ABI_VERSION, .size = sizeof bad,
        .address = request.address, .bytes = request.bytes };
    check(vm_release_on_stack(request.address + request.bytes, &bad) == -RUM_EACCES &&
          vm_release_on_stack(request.address + PAGE, &bad) == -RUM_EACCES &&
          rum_vm_release(request.address, request.bytes) == 0, 39);
    request = packet(PAGE); request.protection = RUM_VM_READ;
    check(rum_vm_allocate(&request) == 0, 15);
    check(rum_syscall3(RUM_SYS_GETCWD, request.address, PAGE, 0) == -RUM_EFAULT &&
          rum_vm_release(request.address, request.bytes) == 0, 16);
}

static void protection_checks(void)
{
    check(raw(RUM_SYS_VM_PROTECT, NULL) == -RUM_EFAULT &&
          raw(RUM_SYS_VM_PROTECT, &readonly_protect) == -RUM_EACCES, 40);
    struct rum_vm_allocate_request control = packet(2 * PAGE);
    check(rum_vm_allocate(&control) == 0, 41);
    struct rum_vm_protect_request *cross = (void *)(uintptr_t)(control.address + PAGE - 16);
    *cross = (struct rum_vm_protect_request){ .version = RUM_VM_ABI_VERSION, .size = sizeof *cross,
        .address = control.address, .bytes = control.bytes, .protection = RUM_VM_READ };
    check(raw(RUM_SYS_VM_PROTECT, cross) == 0 && cross->protection == RUM_VM_READ &&
          raw(RUM_SYS_VM_PROTECT, cross) == 0 && /* Read-only input packet is valid. */
          rum_vm_protect(control.address, control.bytes, RUM_VM_READ | RUM_VM_WRITE) == 0, 42);
    cross->flags = 1;
    check(raw(RUM_SYS_VM_PROTECT, cross) == -RUM_EINVAL, 43);
    cross->flags = 0; cross->reserved[1] = 1;
    check(raw(RUM_SYS_VM_PROTECT, cross) == -RUM_EINVAL, 44);
    cross->reserved[1] = 0; cross->version++;
    check(raw(RUM_SYS_VM_PROTECT, cross) == -RUM_EINVAL, 45);
    cross->version--; cross->size--;
    check(raw(RUM_SYS_VM_PROTECT, cross) == -RUM_EINVAL, 46);
    cross->size++;
    check(rum_syscall3(RUM_SYS_VM_PROTECT, (rum_address_t)(uintptr_t)cross, 1, 0) == -RUM_EINVAL, 47);
    const uint32_t bad_protections[] = {0, RUM_VM_WRITE, 4, 7, UINT32_MAX};
    for (unsigned i = 0; i < sizeof bad_protections / sizeof bad_protections[0]; ++i)
        check(rum_vm_protect(control.address, control.bytes, bad_protections[i]) == -RUM_EINVAL, 48);
    check(rum_vm_protect(control.address + 1, control.bytes, RUM_VM_READ) == -RUM_EINVAL &&
          rum_vm_protect(control.address, PAGE, RUM_VM_READ) == -RUM_EINVAL &&
          rum_vm_protect(control.address, 0, RUM_VM_READ) == -RUM_EINVAL &&
          rum_vm_protect(0xfffff000, 2 * PAGE, RUM_VM_READ) == -RUM_EINVAL, 49);
    check(vm_protect_on_stack(control.address + control.bytes, cross) == -RUM_EACCES &&
          vm_protect_on_stack(control.address + PAGE, cross) == -RUM_EACCES, 50);
    cross->protection |= RUM_VM_WRITE;
    check(vm_protect_on_stack(control.address + control.bytes, cross) == 0 &&
          rum_vm_release(control.address, control.bytes) == 0, 51);

    struct rum_vm_allocate_request left = packet(PAGE), right = packet(PAGE);
    check(rum_vm_allocate(&left) == 0, 52);
    right.address = left.address + PAGE;
    check(rum_vm_allocate(&right) == 0, 53);
    unsigned char *span = (void *)(uintptr_t)(right.address - 16);
    for (unsigned i = 0; i < 32; ++i) span[i] = 'q';
    check(rum_vm_protect(right.address, right.bytes, RUM_VM_READ) == 0 &&
          rum_syscall3(RUM_SYS_GETCWD, (rum_address_t)(uintptr_t)span, 32, 0) == -RUM_EFAULT, 54);
    for (unsigned i = 0; i < 32; ++i) check(span[i] == 'q', 55);
    check(rum_write(RUM_STDOUT, span, 32) == 32 &&
          rum_write(RUM_STDOUT, (void *)(uintptr_t)(right.address + PAGE - 16), 32) == -RUM_EFAULT &&
          raw(RUM_SYS_VM_PROTECT, (void *)(uintptr_t)(right.address + PAGE - 16)) == -RUM_EFAULT, 56);
    check(rum_vm_protect(left.address, 2 * PAGE, RUM_VM_READ) == -RUM_EINVAL &&
          rum_vm_protect(right.address + PAGE, PAGE, RUM_VM_READ) == -RUM_ENOENT, 57);
    span[0] = 'z'; /* Failed combined protection must leave the left page writable. */
    check(rum_vm_protect(right.address, right.bytes, RUM_VM_READ | RUM_VM_WRITE) == 0, 58);
    span[31] = 'z'; /* The warmed read-only TLB entry must now permit writes. */
    check(rum_vm_release(left.address, left.bytes) == 0 && rum_vm_release(right.address, right.bytes) == 0, 59);
}

int main(int argc, char **argv)
{
    check(argc == 3, 20);
    char mode = argv[1][0];
    if (mode == 'i') { invalid(); return 0; }
    if (mode == 'p') { protection_checks(); return 0; }
    if (mode == 'o') {
        struct rum_vm_allocate_request request = packet(2 * PAGE - 1);
        check(rum_vm_allocate(&request) == -RUM_ENOMEM && !request.address && request.bytes == 2 * PAGE - 1, 21);
        return 0;
    }
    if (mode == 'l') {
        unsigned count = 0;
        for (; count < RUM_VM_REGION_LIMIT; ++count) {
            struct rum_vm_allocate_request request = packet(PAGE);
            rum_result_t result = rum_vm_allocate(&request);
            if (result == -RUM_ENOMEM) break;
            check(result == 0, 22); addresses[count] = request.address;
        }
        check(count && count < RUM_VM_REGION_LIMIT, 23);
        check(rum_vm_release(addresses[0], PAGE) == 0, 24);
        struct rum_vm_allocate_request again = packet(PAGE);
        check(rum_vm_allocate(&again) == 0 && again.address == addresses[0], 25);
        for (unsigned i = 0; i < count; ++i) check(rum_vm_release(addresses[i], PAGE) == 0, 26);
        struct rum_vm_allocate_request quota = packet(RUM_ABI_MEMORY_BYTES);
        check(rum_vm_allocate(&quota) == -RUM_ENOMEM, 27);
        return 0;
    }
    struct rum_vm_allocate_request request = packet(8 * PAGE - 1);
    check(rum_vm_allocate(&request) == 0 && request.bytes == 8 * PAGE, 30);
    volatile uint32_t *words = (void *)(uintptr_t)request.address;
    for (unsigned i = 0; i < request.bytes / sizeof *words; ++i) check(words[i] == 0, 31);
    uint32_t seed = (uint32_t)(argv[2][0] - '0') + 1;
    words[0] = seed; words[request.bytes / sizeof *words - 1] = seed ^ 0x11223344;
    check(rum_syscall3(REPORT, request.address, request.bytes, seed) == 0, 32);
    if (mode == 'f') __asm__ volatile ("ud2");
    if (mode == 's') for (;;) __asm__ volatile ("pause" : : : "memory");
    if (mode == 'b' || mode == 'c' || mode == 'k') {
        rum_handle_t input = RUM_STDIN;
        if (mode != 'k') {
            struct rum_pipe_request pipe = { .version = RUM_PIPE_ABI_VERSION, .size = sizeof pipe };
            check(rum_pipe(&pipe) == 0 && rum_syscall3(PIPE_WRITER, pipe.write_handle, 0, 0) == 0, 60);
            input = pipe.read_handle;
        }
        check(rum_read(input, (void *)(uintptr_t)request.address, 1) == 1 &&
              *(unsigned char *)(uintptr_t)request.address == 'Z', 61);
        check(mode == 'b' && rum_vm_protect(request.address, request.bytes, RUM_VM_READ) == 0 &&
              rum_vm_release(request.address, request.bytes) == 0, 62);
        return 0;
    }
    while (rum_syscall3(PEERS, 0, 0, 0) != 2) check(rum_yield() == 0, 33);
    if (mode == 'r') {
        check(rum_vm_protect(request.address, request.bytes, RUM_VM_READ) == 0, 63);
        words[0] = 99;
        check(false, 64);
    }
    if (mode == 'u') {
        check(rum_vm_release(request.address, request.bytes) == 0, 65);
        words[0] = 99;
        check(false, 66);
    }
    check(words[0] == seed && words[request.bytes / sizeof *words - 1] == (seed ^ 0x11223344), 34);
    rum_address_t address = request.address;
    check(rum_vm_release(address, request.bytes) == 0, 35);
    request.address = address;
    check(rum_vm_allocate(&request) == 0, 36);
    for (unsigned i = 0; i < request.bytes / sizeof *words; ++i) check(words[i] == 0, 37);
    /* Deliberately leave the last allocation live for process teardown. */
    return 0;
}
