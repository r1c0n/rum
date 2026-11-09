#include <rum/abi/error.h>
#include <rum/process_limits.h>
#include <rum/vm.h>

static bool protection_valid(uint32_t protection)
{
    return (protection & RUM_VM_READ) && !(protection & ~RUM_VM_PROTECTION_MASK);
}

bool vm_pages_fit(uint32_t committed, uint32_t additional)
{
    const uint32_t limit = RUM_PROCESS_USER_BYTES / RUM_PAGE_SIZE;
    return committed <= limit && additional <= limit - committed;
}

static bool program_range(uint32_t address, uint32_t bytes)
{
    return bytes && !(address % RUM_PAGE_SIZE) && !(bytes % RUM_PAGE_SIZE) &&
           memory_range_contains(RUM_USER_BASE, RUM_USER_PROGRAM_END, address, bytes);
}

static bool overlaps(const struct vm_region *a, const struct vm_region *b)
{
    return a->address < b->address + b->bytes && b->address < a->address + a->bytes;
}

static bool regions_valid(const struct vm_region *regions, uint32_t count)
{
    if (count > RUM_VM_REGION_LIMIT || (count && !regions)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const struct vm_region *region = &regions[i];
        if (!protection_valid(region->protection)) return false;
        switch (region->owner) {
        case VM_REGION_LOADER:
        case VM_REGION_DYNAMIC:
            if (!program_range(region->address, region->bytes)) return false;
            break;
        case VM_REGION_STACK:
            if (!region->bytes || region->address % RUM_PAGE_SIZE || region->bytes % RUM_PAGE_SIZE ||
                !memory_range_contains(RUM_USER_STACK_BASE, RUM_USER_STACK_TOP,
                                       region->address, region->bytes))
                return false;
            break;
        default: return false;
        }
        for (uint32_t j = 0; j < i; ++j)
            if (overlaps(region, &regions[j])) return false;
    }
    return true;
}

rum_result_t vm_prepare_allocate(const struct rum_vm_allocate_request *request,
                                 const struct vm_region *regions, uint32_t count,
                                 uint32_t committed_pages, struct vm_region *output)
{
    if (!request || !output || request->version != RUM_VM_ABI_VERSION ||
        request->size != sizeof *request || request->flags ||
        request->reserved[0] || request->reserved[1] || !request->bytes ||
        !protection_valid(request->protection) || !regions_valid(regions, count))
        return -RUM_EINVAL;
    uint64_t rounded = ((uint64_t)request->bytes + RUM_PAGE_SIZE - 1) &
                       ~((uint64_t)RUM_PAGE_SIZE - 1);
    if (rounded > UINT32_MAX) return -RUM_EOVERFLOW;
    struct vm_region candidate = {request->address ? request->address : RUM_USER_BASE,
                                 (uint32_t)rounded, request->protection, VM_REGION_DYNAMIC};
    if (!program_range(candidate.address, candidate.bytes)) return -RUM_EINVAL;
    if (!vm_pages_fit(committed_pages, candidate.bytes / RUM_PAGE_SIZE) ||
        count == RUM_VM_REGION_LIMIT) return -RUM_ENOMEM;

    /* Unsorted snapshots are accepted. Each collision moves past at least one
       region, so at most count+1 scans are needed for first-fit placement. */
    for (uint32_t pass = 0; pass <= count; ++pass) {
        bool collision = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (!overlaps(&candidate, &regions[i])) continue;
            if (request->address) return -RUM_EEXIST;
            candidate.address = regions[i].address + regions[i].bytes;
            collision = true;
            break;
        }
        if (!collision) {
            *output = candidate;
            return 0;
        }
        if (!program_range(candidate.address, candidate.bytes)) return -RUM_ENOMEM;
    }
    return -RUM_ENOMEM;
}

static rum_result_t owned_region(uint32_t address, uint32_t bytes,
                                 const struct vm_region *regions, uint32_t count,
                                 struct vm_region *output)
{
    if (!output || !bytes || address % RUM_PAGE_SIZE || bytes % RUM_PAGE_SIZE ||
        !regions_valid(regions, count)) return -RUM_EINVAL;
    /* Accept stack geometry only to report an ownership denial, never to
       return it as a mutable dynamic region. Check bounds before overlap math. */
    if (!program_range(address, bytes) &&
        !memory_range_contains(RUM_USER_STACK_BASE, RUM_USER_STACK_TOP, address, bytes))
        return -RUM_EINVAL;
    struct vm_region candidate = {address, bytes, 0, VM_REGION_DYNAMIC};
    for (uint32_t i = 0; i < count; ++i) {
        if (!overlaps(&candidate, &regions[i])) continue;
        if (regions[i].owner != VM_REGION_DYNAMIC) return -RUM_EACCES;
        if (regions[i].address != address || regions[i].bytes != bytes) return -RUM_EINVAL;
        *output = regions[i];
        return 0;
    }
    return -RUM_ENOENT;
}

rum_result_t vm_prepare_protect(const struct rum_vm_protect_request *request,
                                const struct vm_region *regions, uint32_t count,
                                struct vm_region *output)
{
    if (!request || request->version != RUM_VM_ABI_VERSION ||
        request->size != sizeof *request || request->flags ||
        request->reserved[0] || request->reserved[1] ||
        !protection_valid(request->protection)) return -RUM_EINVAL;
    struct vm_region region;
    rum_result_t result = owned_region(request->address, request->bytes, regions, count, &region);
    if (result < 0 || !output) return result < 0 ? result : -RUM_EINVAL;
    region.protection = request->protection;
    *output = region;
    return 0;
}

rum_result_t vm_prepare_release(const struct rum_vm_release_request *request,
                                const struct vm_region *regions, uint32_t count,
                                struct vm_region *output)
{
    if (!request || request->version != RUM_VM_ABI_VERSION ||
        request->size != sizeof *request || request->flags ||
        request->reserved[0] || request->reserved[1] || request->reserved[2])
        return -RUM_EINVAL;
    return owned_region(request->address, request->bytes, regions, count, output);
}
