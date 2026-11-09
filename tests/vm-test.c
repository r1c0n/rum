#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/abi/error.h>
#include <rum/process_limits.h>
#include <rum/vm.h>

#define BASE RUM_USER_BASE
#define PAGE RUM_PAGE_SIZE
#define RW (RUM_VM_READ | RUM_VM_WRITE)

static const struct vm_region untouched = {0x12345678, 73, 91, VM_REGION_STACK};
static const struct vm_region occupied[] = {
    {BASE + 4 * PAGE, 2 * PAGE, RW, VM_REGION_DYNAMIC},
    {RUM_USER_STACK_BASE, RUM_USER_STACK_SIZE, RW, VM_REGION_STACK},
    {BASE, PAGE, RUM_VM_READ, VM_REGION_LOADER},
};
#define COUNT (sizeof occupied / sizeof occupied[0])

static struct rum_vm_allocate_request allocation(uint32_t address, uint32_t bytes)
{
    return (struct rum_vm_allocate_request){RUM_VM_ABI_VERSION,
        sizeof(struct rum_vm_allocate_request), address, bytes, RW, 0, {0, 0}};
}

static void rejected(struct rum_vm_allocate_request request, uint32_t pages, rum_result_t error)
{
    struct vm_region output = untouched;
    struct rum_vm_allocate_request original = request;
    struct vm_region before[COUNT];
    memcpy(before, occupied, sizeof before);
    assert(vm_prepare_allocate(&request, occupied, COUNT, pages, &output) == error);
    assert(!memcmp(&output, &untouched, sizeof output));
    assert(!memcmp(&request, &original, sizeof request));
    assert(!memcmp(before, occupied, sizeof before));
}

static void allocation_checks(void)
{
    struct vm_region output;
    struct rum_vm_allocate_request request = allocation(0, 2 * PAGE - 1);
    assert(vm_prepare_allocate(&request, occupied, COUNT, 19, &output) == 0);
    assert(output.address == BASE + PAGE && output.bytes == 2 * PAGE &&
           output.owner == VM_REGION_DYNAMIC && output.protection == RW);
    request.bytes = 4 * PAGE;
    assert(vm_prepare_allocate(&request, occupied, COUNT, 19, &output) == 0);
    assert(output.address == BASE + 6 * PAGE && output.bytes == 4 * PAGE);
    request = allocation(RUM_USER_PROGRAM_END - PAGE, 1);
    assert(vm_prepare_allocate(&request, occupied, COUNT, 19, &output) == 0);
    assert(output.address == RUM_USER_PROGRAM_END - PAGE && output.bytes == PAGE);
    assert(vm_prepare_allocate(&request, NULL, 0, 0, &output) == 0);

    rejected(allocation(BASE, 1), 19, -RUM_EEXIST);
    rejected(allocation(BASE + 3 * PAGE, 2 * PAGE), 19, -RUM_EEXIST);
    rejected(allocation(BASE + PAGE + 1, 1), 19, -RUM_EINVAL);
    rejected(allocation(0, 0), 19, -RUM_EINVAL);
    rejected(allocation(0, UINT32_MAX), 19, -RUM_EOVERFLOW);
    rejected(allocation(0xFFFFF000, 2 * PAGE), 19, -RUM_EINVAL);
    rejected(allocation(RUM_USER_PROGRAM_END - PAGE, PAGE + 1), 19, -RUM_EINVAL);
    const uint32_t forbidden[] = {PAGE, RUM_HEAP_BASE, RUM_KERNEL_STACK_BASE,
        RUM_USER_STACK_WINDOW_BASE, RUM_USER_STACK_GUARD_BASE,
        RUM_USER_STACK_BASE, RUM_USER_END};
    for (unsigned i = 0; i < sizeof forbidden / sizeof forbidden[0]; ++i)
        rejected(allocation(forbidden[i], PAGE), 19, -RUM_EINVAL);

    request = allocation(0, PAGE);
    request.version++;
    rejected(request, 19, -RUM_EINVAL);
    request = allocation(0, PAGE);
    request.size--;
    rejected(request, 19, -RUM_EINVAL);
    request.size += 2;
    rejected(request, 19, -RUM_EINVAL);
    for (unsigned bit = 0; bit < 32; ++bit) {
        request = allocation(0, PAGE);
        request.flags = 1u << bit;
        rejected(request, 19, -RUM_EINVAL);
    }
    for (unsigned i = 0; i < 2; ++i) {
        request = allocation(0, PAGE);
        request.reserved[i] = 1;
        rejected(request, 19, -RUM_EINVAL);
    }
    const uint32_t protections[] = {0, RUM_VM_WRITE, 4, 7, UINT32_MAX};
    for (unsigned i = 0; i < sizeof protections / sizeof protections[0]; ++i) {
        request = allocation(0, PAGE);
        request.protection = protections[i];
        rejected(request, 19, -RUM_EINVAL);
    }

    const uint32_t limit = RUM_PROCESS_USER_BYTES / PAGE;
    assert(vm_pages_fit(limit, 0) && vm_pages_fit(limit - 1, 1));
    assert(!vm_pages_fit(limit, 1) && !vm_pages_fit(UINT32_MAX, 1) &&
           !vm_pages_fit(1, UINT32_MAX));
    rejected(allocation(0, 1), limit, -RUM_ENOMEM);
    rejected(allocation(0, PAGE + 1), limit - 1, -RUM_ENOMEM);
    rejected(allocation(0, RUM_PROCESS_USER_BYTES + PAGE), 0, -RUM_ENOMEM);
    request = allocation(0, PAGE);
    assert(vm_prepare_allocate(&request, occupied, COUNT, limit - 1, &output) == 0);

    struct vm_region full[RUM_VM_REGION_LIMIT];
    for (unsigned i = 0; i < RUM_VM_REGION_LIMIT; ++i)
        full[i] = (struct vm_region){BASE + i * 2 * PAGE, PAGE, RW, VM_REGION_DYNAMIC};
    output = untouched;
    assert(vm_prepare_allocate(&request, full, RUM_VM_REGION_LIMIT,
                               RUM_VM_REGION_LIMIT, &output) == -RUM_ENOMEM);
    assert(!memcmp(&output, &untouched, sizeof output));
    assert(vm_prepare_allocate(&request, full, UINT32_MAX, 0, &output) == -RUM_EINVAL);
    assert(vm_prepare_allocate(&request, NULL, 1, 0, &output) == -RUM_EINVAL);
    full[1] = full[0];
    assert(vm_prepare_allocate(&request, full, 2, 2, &output) == -RUM_EINVAL);
    assert(vm_prepare_allocate(NULL, NULL, 0, 0, &output) == -RUM_EINVAL);
    assert(vm_prepare_allocate(&request, NULL, 0, 0, NULL) == -RUM_EINVAL);
}

static void ownership_checks(void)
{
    struct rum_vm_protect_request protect = {RUM_VM_ABI_VERSION, sizeof protect,
        BASE + 4 * PAGE, 2 * PAGE, RUM_VM_READ, 0, {0, 0}};
    struct rum_vm_release_request release = {RUM_VM_ABI_VERSION, sizeof release,
        protect.address, protect.bytes, 0, {0, 0, 0}};
    struct vm_region output;
    assert(vm_prepare_protect(&protect, occupied, COUNT, &output) == 0);
    assert(output.protection == RUM_VM_READ && occupied[0].protection == RW);
    assert(vm_prepare_release(&release, occupied, COUNT, &output) == 0);
    assert(output.address == occupied[0].address && output.bytes == occupied[0].bytes);

    const struct { uint32_t address, bytes; rum_result_t error; } cases[] = {
        {BASE + 4 * PAGE, PAGE, -RUM_EINVAL},
        {BASE + 5 * PAGE, PAGE, -RUM_EINVAL},
        {BASE + 3 * PAGE, 4 * PAGE, -RUM_EINVAL},
        {BASE, PAGE, -RUM_EACCES},
        {RUM_USER_STACK_BASE, RUM_USER_STACK_SIZE, -RUM_EACCES},
        {RUM_USER_STACK_GUARD_BASE, PAGE, -RUM_EINVAL},
        {BASE + PAGE, PAGE, -RUM_ENOENT},
        {BASE + 4 * PAGE + 1, PAGE, -RUM_EINVAL},
        {BASE + 4 * PAGE, 1, -RUM_EINVAL},
        {BASE + 4 * PAGE, 0, -RUM_EINVAL},
        {0xFFFFF000, 2 * PAGE, -RUM_EINVAL},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        output = untouched;
        protect.address = release.address = cases[i].address;
        protect.bytes = release.bytes = cases[i].bytes;
        assert(vm_prepare_protect(&protect, occupied, COUNT, &output) == cases[i].error);
        assert(!memcmp(&output, &untouched, sizeof output));
        assert(vm_prepare_release(&release, occupied, COUNT, &output) == cases[i].error);
        assert(!memcmp(&output, &untouched, sizeof output));
    }
    protect.address = release.address = occupied[0].address;
    protect.bytes = release.bytes = occupied[0].bytes;
    protect.protection = RUM_VM_WRITE;
    assert(vm_prepare_protect(&protect, occupied, COUNT, &output) == -RUM_EINVAL);
    protect.protection = RW;
    protect.flags = 1;
    assert(vm_prepare_protect(&protect, occupied, COUNT, &output) == -RUM_EINVAL);
    protect.flags = 0;
    for (unsigned i = 0; i < 2; ++i) {
        protect.reserved[i] = 1;
        assert(vm_prepare_protect(&protect, occupied, COUNT, &output) == -RUM_EINVAL);
        protect.reserved[i] = 0;
    }
    for (unsigned i = 0; i < 3; ++i) {
        release.reserved[i] = 1;
        assert(vm_prepare_release(&release, occupied, COUNT, &output) == -RUM_EINVAL);
        release.reserved[i] = 0;
    }
    release.flags = 1;
    assert(vm_prepare_release(&release, occupied, COUNT, &output) == -RUM_EINVAL);
    release.flags = 0;
    release.version++;
    protect.size--;
    assert(vm_prepare_release(&release, occupied, COUNT, &output) == -RUM_EINVAL);
    assert(vm_prepare_protect(&protect, occupied, COUNT, &output) == -RUM_EINVAL);
}

int main(void)
{
    allocation_checks();
    ownership_checks();
    puts("PASS: VM placement, packet bounds, quotas and region ownership");
    return 0;
}
