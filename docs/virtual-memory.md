# User virtual memory

The memory-region contract is defined in `sdk/include/rum/abi/memory.h`.
Request validation and placement policy live in `kernel/mm/vm.c`; page tables
and physical-frame ownership remain in `arch/i386/paging.c`. This separates
process memory policy from the hardware mapping mechanism.

Applications can allocate, protect and release private regions through the
public runtime. Initial ELF and stack mappings remain owned by the loader.

## Allocate and release

```c
#include <rum/user.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct rum_vm_allocate_request request = {
        .version = RUM_VM_ABI_VERSION, .size = sizeof request,
        .bytes = 8193, .protection = RUM_VM_READ | RUM_VM_WRITE
    };
    if (rum_vm_allocate(&request) < 0) return 1;
    char *bytes = (void *)(uintptr_t)request.address;
    bytes[0] = 'r';
    bytes[8192] = 'm';
    if (rum_vm_protect(request.address, request.bytes, RUM_VM_READ) < 0) {
        (void)rum_vm_release(request.address, request.bytes);
        return 2;
    }
    /* Data is still readable; a direct store now faults this process. */
    return rum_vm_release(request.address, request.bytes) < 0;
}
```

The example owns three zero-filled pages on successful allocation. Release
uses the returned size, 12288 bytes, rather than the original 8193-byte input.
Both functions return zero or a negative error; the high user address is
returned in the packet, not in the signed syscall result register.

Syscall 32 (`RUM_SYS_VM_ALLOCATE`) takes the address of a writable allocation
packet in EBX. Syscall 33 (`RUM_SYS_VM_RELEASE`) takes a readable release packet
in EBX. ECX and EDX must be zero. `rum_vm_release(address, bytes)` constructs its
release packet automatically. Syscall 34 (`RUM_SYS_VM_PROTECT`) takes a readable
protection packet in EBX with ECX/EDX zero. `rum_vm_protect(address, bytes,
protection)` constructs that packet automatically. It returns zero or a negative
error and performs no copyout; the input packet may itself become read-only.
Existing syscall numbers and ABI versions retain their meaning.

## Address ranges and owners

All ranges have an exclusive end. Each process has its own address space.

| Range | Owner and permitted use |
| --- | --- |
| Below `0x80000000` | Supervisor mappings; unavailable to user allocation |
| `0x80000000` to `0xbfc00000` | Loader-owned image mappings and dynamic regions; allocations must not overlap existing regions |
| `0xbfc00000` to `0xbffef000` | Reserved stack-window gap; remains unmapped |
| `0xbffef000` to `0xbfff0000` | User stack guard; remains unmapped |
| `0xbfff0000` to `0xc0000000` | Loader-created 64 KiB user stack; unavailable to region protection/release requests |
| `0xc0000000` and above | Unassigned; unavailable to allocation |

Loader, stack and dynamic regions have distinct owners. An application may
protect or release only a complete dynamic region returned by allocation.
Changing part of a region, combining adjacent allocations into one request,
or changing a loader/stack mapping is rejected. Future partial protection
support needs an explicit contract for splitting records and consuming quota.

Release also rejects a dynamic region containing the saved user ESP, including
an empty downward-growing stack's upper boundary. A program using a dynamic
stack must switch back to another stack before releasing that allocation.
Making that active stack read-only is also rejected. Keeping it read/write is
allowed; the same checks cover an empty stack's upper boundary.

The region snapshot supplied to policy code must describe every owned user
mapping, contain no overlaps, and remain stable through commit. Policy checks
are not a substitute for the paging layer's final collision checks.

## Request packets

Every packet is 32 bytes, uses 32-bit wire fields, and begins with `version`
and `size`. Set these to `RUM_VM_ABI_VERSION` and the exact structure size.
Flags and all reserved words must be zero. No pointer-sized host fields or
kernel pointers appear in the ABI.

| Request | Input | Successful result contract |
| --- | --- | --- |
| `rum_vm_allocate_request` | Address, nonzero byte size, protection | Return zero; write selected address and page-rounded byte size back into the packet |
| `rum_vm_protect_request` | Exact allocated address and rounded size, new protection | Return zero; change the entire dynamic region; no packet output |
| `rum_vm_release_request` | Exact allocated address and rounded size | Return zero; remove the entire dynamic region |

For allocation, address zero selects the lowest available range in the program
window. A nonzero address must be page-aligned and is exact: an occupied range
is an error, never permission to replace it. Sizes round upward to 4096 bytes
using checked wide arithmetic. Zero bytes and wrapping sizes are errors.
Protection and release require aligned addresses and whole-page sizes; they
do not round their inputs.

`RUM_VM_READ` is mandatory and `RUM_VM_WRITE` is optional. The current non-PAE
i386 paging mode enforces user/supervisor and writable/read-only access, but
does not provide an execute-disable bit. Readable user mappings may execute;
read-only does not mean non-executable. No-access, write-only and explicit
execute-control flags are unsupported and rejected. This contract does not
claim hardware-enforced W^X.

| Error | Meaning in region validation |
| --- | --- |
| `EINVAL` | Bad version, packet size, flags, protection, alignment, geometry or partial-region operation |
| `EOVERFLOW` | Allocation size cannot be rounded into the 32-bit size field |
| `EEXIST` | An exact allocation overlaps an existing region |
| `ENOMEM` | Page/region quota exhausted or no suitable virtual range remains |
| `EACCES` | A request attempts to change a loader-owned region, release its active dynamic stack or make that stack read-only |
| `ENOENT` | No owned region overlaps the requested protect/release range |
| `EBUSY` | A syscall retains existing mappings, or mutation is attempted from an unsafe kernel context |

Results use the existing negative rum error values. At the syscall
boundary, invalid user packet storage returns `EFAULT` before any memory
change. Input is copied into trusted kernel storage before validation. The
allocation packet's complete writable range is checked before acquiring
resources. Output publication and region ownership succeed together; failed
operations preserve the input packet, mappings and accounting. A release packet
may live inside the region being released because it has no user-memory output
and is fully copied before unmapping.

## Limits and accounting

The existing committed user-page budget remains 16 MiB per process, including
the 64 KiB stack and all ELF mappings. Dynamic allocations use the remaining
budget. `vm_pages_fit()` is shared by region policy and the existing paging
allocator, with subtraction-based quota checks that cannot wrap.

`RUM_VM_REGION_LIMIT` allows 64 records per process, including loader and stack
records. A private `vm_region` is 16 bytes. Each private address-space record
embeds 1024 bytes of region records and a 4-byte count, included in its live
`metadata_bytes`. No heap allocation is needed to register an individual region.
The record limit is independent of the page budget.

Trusted page allocation records image/stack ownership. Trusted protection or
partial release preflights any required record splits before changing page
tables; exhausting record capacity leaves both unchanged. Separate dynamic
allocations never merge merely because they are adjacent.

Paging snapshots account for the resources that exist today:

| Counter | Ownership |
| --- | --- |
| `user_pages` | Committed user data/image/stack frames; charged against the 16 MiB budget |
| `private_table_pages` | Address-space-owned page-table frames; consume physical RAM separately |
| `directory_pages` | One physical page per registered address space, including the kernel |
| `metadata_bytes` | Heap payload of live private paging-space records; excludes heap allocator headers and the static kernel record |
| `shared_table_pages` | Kernel page tables counted once globally, borrowed by private spaces |
| `regions` / `dynamic_regions` | Live registry entries, and the subset owned by application allocations |
| `buffer_users` | Thread-owned syscall mapping leases, including blocked operations |

The 1 GiB user window can require at most 256 private table pages. These and
directory pages consume PMM frames outside the committed-user-page quota.
Metadata consumes kernel heap storage; it is not an additional PMM allocation
to add again to heap backing frames. Guard/reserved addresses consume no data
frames. Kernel entry stacks are accounted separately by task ownership.

Snapshots are allocation-free and IRQ-safe. Global metadata bytes equal the
sum of live private-space payloads. Per-space snapshots report one directory,
its private tables and committed pages, and its metadata payload. Destruction
and failed construction must return all owned counts to their prior values.
Physical RAM or heap metadata may run out before a policy quota is reached.

## Scheduling and transaction boundaries

Public memory operations run only in task context, with interrupts enabled
on entry, outside IRQ handlers and active synchronous filesystem/block work.
The syscall holds task switching from packet validation through mapping/registry
commit or rollback and output publication. No yield, wait or user return is
allowed while ownership is incomplete. The hold ends only after state is
consistent; cancellation or rescheduling can then be serviced at the ordinary
safe boundary.

The pure policy functions allocate nothing and change no interrupt state.
They accept trusted snapshots, return a plan only on success, and never reserve
addresses or physical memory. Callers must retain serialization through commit
and still handle allocation failures after successful validation.

Low-level paging mutations serialize with interrupt-save/restore. Allocation
checks the full range first, zeros every data frame and publishes its region
only after success. Failure rolls back all new pages and empty private tables.
Release invalidates active translations before reusing frames; inactive spaces
receive a CR3 reload before they next execute. Address-space destruction frees
all remaining regions and requires an inactive space, after switching away from
its CR3 and stack. An unpublished process remains its producer's responsibility
if task construction fails.

These synchronous transactions do not introduce kernel preemption. A request
may touch up to the 4096-page process budget while interrupts are disabled,
so large mapping operations can delay timer delivery. Region-policy scans are
bounded by the 64-record limit.

Every syscall except exit and the three memory operations acquires a thread-owned
mapping lease before it validates or copies user storage. The lease retains all
existing user mappings and their permissions until the call returns, even if it
blocks. Syscall copies still validate every page for their required access;
retention does not make an invalid pointer valid or freeze buffer contents.

Public release and protection first validate the request and ownership, then
return `EBUSY` while any lease remains. They neither modify memory nor wait for
the holder. New allocations remain safe because they cannot replace existing
mappings. Space destruction also refuses a retained space. Other address spaces
are unaffected, and leases do not hold task switching or interrupts across waits.

The common syscall return drops its lease before returning to user mode.
Termination releases a thread's outstanding lease after handle cleanup when
cancellation abandons a blocked kernel continuation. The thread records whether
it owns a lease so termination and normal completion cannot release it twice.
The last release permits subsequent mapping changes; counter underflow and
overflow are refused.

There is still one user thread per private address space. The lease is deliberately
coarse: even calls that finish copying their packets before a wait retain the
entire user mapping set. Future user threads must preserve this exclusion rule
or replace it with explicit per-range retention, checked under the same memory
ownership lock as mutation. A future nonblocking release/protection can return
`EBUSY` while another thread's syscall is outstanding; callers must handle that
result. Shared address spaces and SMP require an additional concurrency audit.
Future asynchronous I/O must transfer retention to the request until completion;
returning from a syscall alone cannot end that request's buffer lifetime.

Trusted raw paging mutation is reserved for loading/administering spaces and
deliberate kernel fault-injection fixtures; it bypasses the public lease check.
Production code must not use it to invalidate retained syscall buffers. See
[Scheduling safety](scheduling.md) and [Memory map and ownership](memory-layout.md).

## Focused checks

Run `make -j4 test-vm-host` in Ubuntu/WSL for packet, placement, quota and
ownership checks plus the shared memory-layout/ABI checks. Rejected policy
requests preserve their inputs, region snapshots and output records.

The existing `paging-ok` guest fixture also exercises the production page
allocator's quota, zeroing, cross-page copies, isolation and physical-memory
failure rollback. It checks per-space metadata and directory accounting,
including cleanup, against global ownership snapshots.

Run `make -j4 test-vm` for those host checks, the paging fixture at 64 MiB, and
ring-3 region tests through ELF at 16 MiB and ISO at 64 MiB. The ring-3 cases
cover malformed/cross-page packets, read-only output, live-region isolation,
quota reuse, physical OOM, failed process construction and repeated exit, fault
and CPU-bound cancellation. Protection cases verify read-only input packets,
self-protection, whole-region validation, cross-page read/write permissions,
active stacks, restored writable TLB entries and exact hardware page faults.
Blocked pipe/console cases check exclusion, wakeup, cancellation, lease cleanup
and unrelated-space progress. They also run with the normal lifecycle test group.
This does not run the full regression suite.

The build launchers expose the same focused group:

```sh
./rum test --suite vm -j4
```

```powershell
.\rum.ps1 test -Suite vm -Jobs 4
```

For the hardware permission bits used here, see [OSDev's x86 paging reference](https://wiki.osdev.org/Paging).
