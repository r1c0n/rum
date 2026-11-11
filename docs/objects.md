# Kernel object ownership

The common object core gives kernel resources a type, a construction state and
explicit reference ownership. Its implementation is in `kernel/object/`; the
private interface is `kernel/include/rum/object.h`. Applications never receive
these headers, addresses, callback pointers or layouts.

The core manages lifetime, not names or access rights. Resource implementations
still own their payload and backend operations. Existing stream descriptors,
open descriptions, pipes and process records currently use their established
ownership paths; their counts remain in their subsystem diagnostics. Common
object counts cover only resources registered with this core.

The [typed handle table](object-handles.md) uses this core for independent
per-entry grants, generations and operation references. Its public namespace
is separate from the current stream-descriptor interface.

## Entries, descriptions and references

| Layer | Responsibility |
| --- | --- |
| Named filesystem entry | Gives a backend node a path; removal and replacement follow filesystem identity and open-reference rules |
| Open description | Retains the backend identity and shares offset, access mode, append state and deferred errors between aliases |
| Descriptor/table entry | Owns one reference to a description; slot identity and close-on-spawn flags belong to that entry |
| In-flight operation | Owns an independent reference until completion or cancellation has stopped using the payload |
| Kernel object header | Records the type, publication state and references held by those owners |

Closing one table entry consumes its reference. Another alias or operation can
keep the payload alive. A raw cached pointer does not retain anything: acquire
an operation reference while a table entry or another legitimate owner still
protects the object, before dropping that protection or blocking.

Reference classes are `OBJECT_OWNER`, `OBJECT_TABLE` and `OBJECT_OPERATION`.
They distinguish construction/service ownership, installed entries and active
work. Moving a reference between classes does not create another owner; adding
an alias or operation does. These classes confer no access rights themselves.

## Construction and publication

1. Allocate fresh storage and zero its header. Keep the object private.
2. Call `object_initialize` with a static, immutable type descriptor and a
   destructor. Initialization owns one construction reference.
3. Initialize every payload field and acquire required backend resources. A
   failure releases the construction reference; the destructor must handle
   partially constructed payloads, including resources not yet acquired.
4. Call `object_publish` with the first owner's reference class. This transfers
   the construction reference rather than incrementing the count. When an
   entry must become visible atomically, publish and install its pointer in the
   same short interrupt-protected update.

The core rejects acquiring or transferring references to an unpublished object.
Repeated publication and reinitialization of an occupied header fail. The core
cannot infer whether a type's payload is complete; checking it before publication
is the constructor's responsibility.

The private type categories cover files, directories, pipe readers/writers,
console input/output and process references. A type descriptor selects the
category and cleanup callback. It must outlive every instance and callback;
types and callback addresses are never part of the userspace ABI.

## Acquisition, transfer and final release

`object_acquire` adds one reference to a published object. `object_transfer`
reclassifies an existing reference without changing the total. Both require a
pointer already protected by a legitimate owner. Invalid classes, a missing
source reference and acquisition beyond the reference limit are rejected.

`object_release` consumes one reference on success. A nonfinal release only
updates accounting. The final release marks the object as destroying before
calling its destructor. Acquisition, publication and further release are refused
while destruction is active, including when the callback yields or blocks.

The destructor returns zero only after consuming the payload. It may free the
containing allocation; the core uses saved type/accounting data afterward and
never reads that header again. Storage retained for later reuse must have its
header cleared, and cannot be recycled until the callback has returned.

A negative destructor result preserves the final reference and restores the
previous constructing/published state. The caller still owns that reference
and must retry or retain it for later cleanup. The callback must leave the
header intact and enough payload state to retry safely. It must not free the
allocation on failure. Successful partial backend cleanup may need to be
recorded in the payload so a retry does not close a resource twice.

This contract does not roll back disk writes or add filesystem crash recovery.
Backend errors and durability still follow [FAT16 guarantees](fat16.md).
There is no implicit cleanup queue or silent disposal of failed final closes.

## Allowed contexts

Ownership mutation is refused in IRQ handlers. IRQ code may take the bounded
count snapshot, signal an event or request task work; it must leave reference
release and resource cleanup to task context.

Acquisition, publication, transfer and nonfinal release perform only bounded
bookkeeping and can run with interrupts disabled. They allocate nothing, do
not call back into a backend and never switch tasks.

Initialization and final destruction require `task_blocking_allowed()`:

- The task system is initialized and the current task is running and not idle.
- Interrupts are enabled and execution is outside an IRQ handler.
- No short task-switch hold is active.
- No synchronous filesystem operation or block request is active.

Final destruction with interrupts disabled or inside any of those guards returns
`EBUSY` without consuming the last reference. The callback runs after the core's
short protected update has ended. It may perform filesystem work or wait when
its type-specific lock rules allow it, and must preserve interrupt state.
Callers must also release any other lock incompatible with that destructor;
the core cannot discover arbitrary subsystem locks.

Serialization uses rum's single-CPU, cooperative kernel execution and short
interrupt-protected updates. This is not an SMP or arbitrary kernel-preemption
locking contract.

## Bounds and diagnostics

The core permits at most 1024 allocated objects, including objects under
construction or destruction, and 65535 references per object. These are private
limits for registered objects, not changes to the current stream-descriptor
limit. Capacity failure returns `ENOMEM`; reference saturation returns
`EOVERFLOW`. Invalid inputs and reference underflow leave accounting unchanged.

`object_stats()` copies aggregate and per-type counters without allocating,
following backend references or returning addresses. The counters distinguish
constructing, published and destroying objects and each reference class. Objects
and their last references remain counted until a destructor succeeds.

The recovery console's `diag` output includes:

```text
Objects: 0 allocated | 0 published, 0 constructing, 0 destroying
Object refs: 0 owners, 0 table entries, 0 operations
```

These lines describe the common core. The existing `Handles`, `Filesystem`,
`Pipes` and process lines continue to report resources on their current paths.

## Verification

Run the focused host and guest checks:

```powershell
.\rum.ps1 test -Suite objects -Jobs 4
```

```sh
./rum test --suite objects -j4
make test-objects-host
```

Host checks cover reference underflow/saturation, invalid publication, ownership
transfer, object capacity, failed construction, retryable destruction and
destructors that free their containing allocation. Guest checks use real task,
PIT/IRQ, filesystem and block guards through ELF/16 MiB and ISO/64 MiB boot.
They close a table reference while a worker retains file I/O, yield inside the
final destructor, inspect destroying-state accounting and check backend, heap
and page counts after cleanup. The guest backend is a private test fixture;
no developer disk is attached or changed.

The default host suite includes the ownership checks, and the lifecycle group
includes the two guest boots.
