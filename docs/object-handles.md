# Typed object handles

The object-handle interface adds generation-aware identities and access masks
on top of [common object ownership](objects.md). Public definitions live in
`sdk/include/rum/abi/object.h`; applications include `<rum/user.h>` for wrappers.
The table implementation is private to `kernel/object/handles.c`.

Trusted resource services grant these handles. The public interface currently
provides query, duplication and close; filesystem, pipe, console and process
resource adapters are separate roadmap work. `rum_open` continues to return a
stream descriptor, not an object handle.

## Two namespaces

| Property | Stream descriptor | Object handle |
| --- | --- | --- |
| C type | `rum_handle_t` | `rum_object_handle_t` |
| Capacity | 32 per process, including standard streams | 32 additional entries per process |
| Identity | Reusable slot; 0–2 are standard streams | Opaque unsigned 32-bit token with a generation |
| Invalid value | Unoccupied/out-of-range slot | Zero, a stale token or a value outside this namespace |
| Operations | Existing read/write/seek/close/duplicate and mapped spawn calls | Object query/duplicate/close and private typed operation acquisition |
| Inheritance | Existing stream/mapping/close-on-spawn rules | Explicit `RUM_OBJECT_INHERIT` flag |

The kernel rejects descriptors passed to object calls and tokens passed to
descriptor calls. The old descriptor ABI, shared offsets, append/deferred-error
state and `RUM_DUP_CLOSPAWN` behavior remain unchanged. Descriptor 3 can still
be reused after close; it is not a durable identity.

Tokens belong to the current process's table. Copying a number to another
process does not grant a reference or access to the sender's table. Inheritance
preserves numeric tokens in the child while creating independent references.
Tokens are not cryptographic secrets: authority comes from the validated entry
and its grant in that process.

## Generations and exhaustion

Keep all 32 bits. Valid tokens can have the sign bit set, including `UINT32_MAX`.
They use unsigned packet outputs, not the signed system-call result. Do not
mask them, cast them to signed errors or interpret the encoding.

Privately, a token has a namespace tag, five slot bits and a nonzero 26-bit
generation. Allocation advances that slot's generation. Close clears the entry
without resetting its history. After generation 67108863 is used and closed,
the slot retires for the rest of that table's lifetime; it never wraps.
Inheritance copies generation history, including retired and empty slots.

A full table or one with no reusable slots returns `EMFILE`. There is no API
to reset a live process's history and make a stale token valid again.

## Types and grants

| Type | Accepted `RUM_RIGHT_*` bits |
| --- | --- |
| File | QUERY, DUPLICATE, READ, WRITE, SEEK, FLUSH |
| Directory | QUERY, DUPLICATE, ENUMERATE |
| Pipe reader / console input | QUERY, DUPLICATE, READ |
| Pipe writer / console output | QUERY, DUPLICATE, WRITE |
| Process | QUERY, DUPLICATE, WAIT, TERMINATE |

These masks define grants accepted by the table; they do not add type-specific
I/O or process-management calls themselves. Object QUERY returns the entry's
type, rights and inheritance flag, not file contents, process memory, backend
identifiers or kernel addresses.

`object_handle_install` accepts a type-valid grant only when it is a subset of
the rights authorized by the trusted resource service. That service must derive
authorization from verified open modes or management policy, rather than simply
copy requested bits. Success transfers one published OWNER reference to a TABLE
reference. Failure leaves ownership with the producer.

QUERY requires `RUM_RIGHT_QUERY`. DUPLICATE requires `RUM_RIGHT_DUPLICATE` and
can preserve or reduce the source rights, never add rights. Zero rights are
allowed: such an entry can be closed but cannot query or duplicate itself.
CLOSE requires no access right beyond ownership of a valid entry.

Calls accept `RUM_OBJECT_ANY` or an expected concrete type. Wrong types and
invalid rights are rejected before acquiring a resource reference. Private
services call `object_handle_acquire` with the expected type and required rights;
success returns an OPERATION reference, which must remain owned until all
payload use, including blocked work, has ended.

## Query, duplicate and close

The convenience wrappers return zero or a negative public error:

```c
struct rum_object_information info;
rum_result_t result = rum_object_query(handle, RUM_OBJECT_FILE, &info);
if (result < 0) return result;

rum_object_handle_t reader;
result = rum_object_duplicate(handle, RUM_OBJECT_FILE,
                             RUM_RIGHT_QUERY | RUM_RIGHT_READ, 0, &reader);
if (result < 0) return result;
return rum_object_close(reader);
```

The supplied handle must already grant DUPLICATE and the requested rights.
Each alias owns its grant and flags; reducing one does not change the source.
Application output pointers must refer to writable storage, as for other C
wrappers. `rum_object_control` exposes the underlying packet directly.

Syscall 36 (`RUM_SYS_OBJECT`) takes a writable 48-byte `rum_object_request` in
EBX; ECX and EDX must be zero. Set `version = RUM_OBJECT_ABI_VERSION`, `size`
to the exact structure size and every reserved/output-only field to zero.

| Operation | Inputs beyond version/size | Outputs |
| --- | --- | --- |
| QUERY | Handle and expected type; rights/flags/result_handle zero | Actual type, rights and flags |
| DUPLICATE | Handle, expected type, rights and flags | New token in result_handle |
| CLOSE | Handle and expected type; rights/flags/result_handle zero | No additional output |

The only flag is `RUM_OBJECT_INHERIT`. Every packet byte is validated across
page boundaries before ownership changes. Failed requests leave outputs and
table ownership unchanged. A syscall mapping lease protects the packet until
copyout completes, including across a yielding final destructor.

## Inheritance and cleanup

Object entries are non-inheritable by default. Explicitly inheritable entries
pass through RUN, SPAWN and mapped SPAWN with the same tokens and rights. A
trusted inheritance ceiling can reduce grants. Other entries are omitted.
Creating an inheritable alias through DUPLICATE still requires duplication
rights. Stream mappings neither add object rights nor convert tokens to slots.

The child table stays private until its references and process resources are
ready. Inheritance failure rolls back every reference acquired so far. A supplied
private producer table transfers only on process publication; failed construction
leaves it owned by that producer.

Exit, user fault and cancellation close the table before execution resources
are reaped. A parent's close does not revoke a child's reference. Entries being
closed reject further operations until cleanup finishes. A failed final
destructor preserves the entry, token, grant and last reference for retry.
Reaping refuses a record that still owns entries.

Rights are checked on acquisition/operation. There is no revocation or operation
to silently change another alias's grant. An operation reference retains
lifetime, not authority for unrelated operations.

## Errors and compatibility

| Error | Meaning |
| --- | --- |
| EINVAL | Bad packet/version/size, unknown bits or unsupported/wrong type |
| EBADF | Invalid, stale, closed or descriptor-namespace value |
| EACCES | Missing rights or a grant that would increase authority |
| EMFILE | Full table or no unretired slots |
| EOVERFLOW | Common-object reference limit exhausted |
| EFAULT | Packet storage is not wholly readable/writable user memory |
| EBUSY | IRQ use, an entry already closing or unsafe final destruction |

Destructor errors propagate while retaining ownership. They do not promise
rollback of backend writes; resource implementations retain their own failure
contracts.

Existing syscall numbers and layouts are unchanged, with call 36 added after
CLOCK. The syscall ABI and object packet family are both version 1. A released
0.7 kernel returns `ENOSYS` for the new call. Existing 0.7 applications retain
their descriptor, memory and runtime interfaces on the newer kernel.

## Focused checks

```powershell
.\rum.ps1 test -Suite object-handles -Jobs 4
```

```sh
./rum test --suite object-handles -j4
make test-object-handles-host
```

Host checks cover grants, types, malformed packets, stale/forged tokens,
capacity, generation retirement, reduced inheritance, reference saturation and
retryable close. Ring-3 checks use ELF/16 MiB and ISO/64 MiB, including cross-page
and partially read-only packets, legacy shared offsets, process inheritance,
CPU-bound cancellation, faults and failed construction. Repeated runs return
object, table, descriptor, heap and page counts to baseline. The default host
and lifecycle groups include these checks.
