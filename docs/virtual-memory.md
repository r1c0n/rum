# User virtual memory

The memory-region contract is defined in `sdk/include/rum/abi/memory.h`.
Request validation and placement policy live in `kernel/mm/vm.c`; page tables
and physical-frame ownership remain in `arch/i386/paging.c`. This separates
process memory policy from the hardware mapping mechanism.

The packet definitions and kernel policy are available now. Allocation,
protection and release syscall entry points and runtime wrappers are not yet
available. Applications still use the mappings established by the ELF loader.

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
| `rum_vm_protect_request` | Exact allocated address and rounded size, new protection | Return zero; change the entire dynamic region |
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
| `EACCES` | A request attempts to change a loader-owned image or stack region |
| `ENOENT` | No owned region overlaps the requested protect/release range |

Results use the existing negative rum error values. At the eventual syscall
boundary, invalid user packet storage must return `EFAULT` before any memory
change. Copy input into trusted kernel storage first and validate every field.
For allocation, validate the complete writable result packet before acquiring
resources; output publication and region ownership must succeed together.
Failed operations must preserve the input packet, mappings and accounting.

## Limits and accounting

The existing committed user-page budget remains 16 MiB per process, including
the 64 KiB stack and all ELF mappings. Dynamic allocations use the remaining
budget. `vm_pages_fit()` is shared by region policy and the existing paging
allocator, with subtraction-based quota checks that cannot wrap.

`RUM_VM_REGION_LIMIT` allows 64 records per process, including loader and stack
records. A private `vm_region` is 16 bytes, so the future full registry payload
is bounded at 1024 bytes. Region registration and its storage are part of the
allocation implementation; those bytes are not allocated or reported as live
metadata yet. The record limit is independent of the page budget.

Paging snapshots account for the resources that exist today:

| Counter | Ownership |
| --- | --- |
| `user_pages` | Committed user data/image/stack frames; charged against the 16 MiB budget |
| `private_table_pages` | Address-space-owned page-table frames; consume physical RAM separately |
| `directory_pages` | One physical page per registered address space, including the kernel |
| `metadata_bytes` | Heap payload of live private paging-space records; excludes heap allocator headers and the static kernel record |
| `shared_table_pages` | Kernel page tables counted once globally, borrowed by private spaces |

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

Public memory operations will run only in task context, with interrupts enabled
on entry, outside IRQ handlers and active synchronous filesystem/block work.
Use a process-owned non-switching scope from snapshot validation through
mapping/registry commit or rollback. No yield, wait or user return is allowed
while ownership is incomplete. Release the scope only after temporary resources
are disposed of and state is consistent; pending cancellation or rescheduling
can then be serviced at the ordinary safe boundary.

The pure policy functions allocate nothing and change no interrupt state.
They accept trusted snapshots, return a plan only on success, and never reserve
addresses or physical memory. Callers must retain serialization through commit
and still handle allocation failures after successful validation.

Existing low-level paging mutations serialize with interrupt-save/restore and
invalidate active translations before reusing frames. This release step does
not introduce kernel preemption or change those critical sections. Large
synchronous mapping operations can delay timer delivery; later allocation work
must keep that bound documented. Final address-space destruction still requires
an inactive space, after switching away from its CR3 and stack.

Before adding public mutation calls, define how blocked operations retain their
user buffers, including when user threading later permits concurrent changes.
Do not allow release or protection to invalidate a buffer still in use by the
kernel. See [Scheduling safety](scheduling.md) and [Memory map and ownership](memory-layout.md).

## Focused checks

Run `make -j4 test-vm-host` in Ubuntu/WSL for packet, placement, quota and
ownership checks plus the shared memory-layout/ABI checks. Rejected policy
requests preserve their inputs, region snapshots and output records.

The existing `paging-ok` guest fixture also exercises the production page
allocator's quota, zeroing, cross-page copies, isolation and physical-memory
failure rollback. It checks per-space metadata and directory accounting,
including cleanup, against global ownership snapshots.

Run `make -j4 test-vm` to build and run those host checks and the focused guest
fixture at 64 MiB. This does not run the full regression suite.
