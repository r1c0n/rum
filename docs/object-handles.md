# Typed object handles

The object-handle interface adds generation-aware identities and access masks
on top of [common object ownership](objects.md). Public definitions live in
`sdk/include/rum/abi/object.h`; applications include `<rum/user.h>` for wrappers.
The table implementation is private to `kernel/object/handles.c`.

Use `rum_object_from_descriptor` to obtain a typed reference to an open file,
directory, pipe endpoint or console stream. Both interfaces share the same open
description. `rum_open` and `rum_pipe` continue to return stream descriptors.
Use [process handles](process-objects.md) for authorized process query, wait and
termination, including retained status after collection.

## Two namespaces

| Property | Stream descriptor | Object handle |
| --- | --- | --- |
| C type | `rum_handle_t` | `rum_object_handle_t` |
| Capacity | 32 per process, including standard streams | 32 additional entries per process |
| Identity | Reusable slot; 0–2 are standard streams | Opaque unsigned 32-bit token with a generation |
| Invalid value | Unoccupied/out-of-range slot | Zero, a stale token or a value outside this namespace |
| Operations | Existing read/write/seek/close/duplicate and mapped spawn calls | Query/duplicate/close, read/write, file seek/flush, directory iteration and isatty |
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

Object QUERY returns the entry's
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
| FROM_DESCRIPTOR | Stream descriptor in handle, expected type, requested rights and flags | New token in result_handle |

The only flag is `RUM_OBJECT_INHERIT`. Every packet byte is validated across
page boundaries before ownership changes. Failed requests leave outputs and
table ownership unchanged. A syscall mapping lease protects the packet until
copyout completes, including across a yielding final destructor.

## Sharing an open stream

```c
rum_result_t fd = rum_open("/NOTES.TXT", RUM_OPEN_READ, 0);
if (fd < 0) return fd;
rum_object_handle_t reader;
rum_result_t result = rum_object_from_descriptor((rum_handle_t)fd,
    RUM_OBJECT_FILE, RUM_RIGHT_READ, 0, &reader);
rum_close((rum_handle_t)fd);
if (result < 0) return result;
char bytes[64];
result = rum_object_read(reader, bytes, sizeof bytes);
rum_object_close(reader);
return result;
```

Export adds one table reference, not another backend open or pipe endpoint. It
does not consume or change the descriptor. Separate exports, descriptor aliases
and inherited typed handles share the offset, append mode, directory cursor and
deferred error. Independent opens remain independent. Closing either interface
leaves the other usable. Removal, rename and replacement retain the filesystem's
existing busy-identity rules until the final reference is released.

The grant must fit the verified open mode. Files authorize SEEK and FLUSH plus
READ/WRITE from their access mode; directories authorize ENUMERATE; pipe and
console endpoints authorize only their direction. QUERY and DUPLICATE can also
be requested. No rights are implied: request QUERY if using `rum_object_isatty`,
and DUPLICATE if another typed alias is needed.

A descriptor marked `RUM_DUP_CLOSPAWN` cannot be exported directly with INHERIT.
It can be exported without that flag. As with explicit descriptor duplication,
an authorized caller can deliberately create an inheritable alias later;
close-on-spawn is per entry, not irrevocable authority.

## Typed I/O

| Wrapper | Required right | Result |
| --- | --- | --- |
| `rum_object_read(handle, buffer, bytes)` | READ | Bytes read; zero at EOF |
| `rum_object_write(handle, buffer, bytes)` | WRITE | Bytes written |
| `rum_object_seek(handle, displacement, whence, position)` | SEEK | Zero; absolute 64-bit position output |
| `rum_object_readdir(handle, entry)` | ENUMERATE | 1 entry, 0 at end |
| `rum_object_flush(handle)` | FLUSH | Zero after backend flush |
| `rum_object_isatty(handle)` | QUERY | 1 for console streams, 0 otherwise |

Negative results use public `RUM_*` errors. Seek is for regular files; directory
iteration uses the existing versioned `rum_directory_entry`. Reopen a directory
to get a fresh cursor, or rewind through a retained legacy descriptor. These
wrappers do not add file metadata or terminal control APIs. Process management
uses the separate [process-object packet](process-objects.md#public-packets).

Syscall 37 (`RUM_SYS_OBJECT_IO`) takes a writable 48-byte
`rum_object_io_request` in EBX, with ECX/EDX zero. Initialize version to
`RUM_OBJECT_IO_VERSION`, size to 48, operation, token and an expected type (or
ANY). Zero all unused and reserved fields:

- READ/WRITE use `data` and `bytes`.
- SEEK uses `whence` and a signed two's-complement displacement in
  `offset_hi:offset_lo`; success replaces those words with the unsigned position.
- READDIR uses `data` pointing to a writable directory-entry packet. The entry
  cannot overlap the control packet.
- FLUSH/ISATTY have no extra inputs.

The control packet must be readable and writable throughout, including calls
with no output. Complete transfer ranges and directory packets are checked
before backend work. Zero-length transfers still check type and rights, but
ignore the buffer. Calls preserve the existing 512-byte file/pipe and 128-byte
console transfer caps, shared deferred-error behavior, pipe atomic-write limit,
EOF and EPIPE semantics. Callers must handle short transfers.

Acquiring a console handle grants no foreground input authority. Reads still
check the current process's console scope, and output follows the same terminal
session policy as descriptor writes. `isatty` only identifies a stream.

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

Each I/O syscall owns an OPERATION reference registered on its current thread.
Normal return releases it before the user-buffer lease. Cancellation of a
blocked console call can abandon its C continuation; termination releases the
same reference before closing either table. Pipe calls unwind their internal
wait/event pins before that release. Reaping refuses an outstanding operation.

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
| ENOSYS | Unsupported operation or an object without an I/O adapter |

Destructor errors propagate while retaining ownership. They do not promise
rollback of backend writes; resource implementations retain their own failure
contracts.

Existing syscall numbers and layouts are unchanged, with calls 36 and 37 added
after CLOCK. The syscall ABI and object packet families are version 1.
Process-object operations use call 38 and their own versioned packet family.
A released 0.7 kernel returns `ENOSYS` for these calls. Existing 0.7 applications
retain their descriptor, memory and runtime interfaces on the newer kernel.

## Focused checks

```powershell
.\rum.ps1 test -Suite object-handles -Jobs 4
```

```sh
./rum test --suite object-handles -j4
make test-object-handles-host
```

Run `handles` and `pipes` for mixed descriptor/typed I/O, close during blocked
operations, console cancellation and resource baselines. The `sdk` checks build
outside the repository and compare FAT file bytes with host tools after exit.

Host checks cover grants, types, malformed packets, stale/forged tokens,
capacity, generation retirement, reduced inheritance, reference saturation and
retryable close. Ring-3 checks use ELF/16 MiB and ISO/64 MiB, including cross-page
and partially read-only packets, legacy shared offsets, process inheritance,
CPU-bound cancellation, faults and failed construction. Repeated runs return
object, table, descriptor, heap and page counts to baseline. The default host
and lifecycle groups include these checks.
