# Resource counts

Use `handles` to inspect the resources passed to a command:

```text
/> handles
This process: 3 descriptors, 0 typed handles
3 objects, 3 table references
  console inputs: 1
  console outputs: 2
```

The command reports its own tables, not the shell's or the entire system's.
Pipelines and redirection change the kinds of objects it receives. For example,
`handles | cat` reports a pipe writer instead of a console output for stdout.
Use `recovery`, then `diag`, for the trusted kernel's full resource ledger.

## Application interface

`rum_resources(&information)` fills a `struct rum_resource_information` from
`<rum/abi/resources.h>` and returns zero or a negative error. It reads only the
caller's descriptor and typed-handle tables. There is no target process argument.

| Field | Meaning |
| --- | --- |
| `descriptors` | Occupied legacy descriptor entries, including standard streams |
| `typed_handles` | Occupied generation-aware handle entries |
| `objects` | Distinct objects referenced by either of those tables |
| `table_references` | Local table entries retaining those objects; equals descriptors plus typed handles |
| `types[8]` | Distinct local objects by `RUM_OBJECT_*` type; index zero stays zero |

An alias adds a table reference but does not add an object. Opening the same file
again creates another description and therefore another object. Pipe reader and
writer endpoints are separate objects. A process object can remain after its
execution resources have been reaped, until its last legitimate owner closes it.

Counts do not include another process's aliases, global references, temporary
operation references, unpublished constructors or loader buffers. Tokens,
addresses, paths, offsets, names and other processes' private data are absent.
The snapshot acquires no references, allocates nothing, invokes no backend and
does not change offsets, errors or ownership. It scans at most 64 entries under
one short interrupt guard. The current implementation has one user thread per
process; threading will need to preserve this table-snapshot contract.

## Wire contract

Syscall **39**, `RUM_SYS_RESOURCES`, uses EBX as a readable/writable 80-byte
packet address; ECX and EDX must be zero. The packet contains 20 fixed-width
32-bit words:

```text
version, size, flags, reserved,
descriptors, typed_handles, objects, table_references,
types[8], reserved_tail[4]
```

Raw callers set version `RUM_RESOURCE_VERSION` (1), size 80 and every other
word to zero. The SDK wrapper initializes that request on each call. Unknown
versions/sizes, nonzero output words, flags or reserved words return `EINVAL`.
Invalid, unmapped, overflowing or non-writable spans return `EFAULT`, including
packets crossing a protected page boundary. Validation completes before any
snapshot is copied out. A rejected syscall leaves the packet and ownership
unchanged. Successful reserved words remain zero.

This append-only ABI addition does not change calls 0–38 or existing packet
layouts. An older kernel returns `ENOSYS`; `local-resource-counts-v1` in the SDK
manifest identifies the requirement. Released 0.7 applications continue using
their original headers, startup and runtime.

## Verification

The shared-handle host checks distinguish aliases from unique objects, exclude
a second process's table, fill all 32 typed slots and verify that a snapshot
does not acquire references or touch the filesystem. The external SDK workload
combines cross-page packets, malformed fields, read-only output, capacity,
wrong types, rights denial and stale tokens with concurrent file I/O, process
faults and pipe cancellation. After owners close, both caller-local snapshots
and recovery's global resource ledger return to their baseline.
