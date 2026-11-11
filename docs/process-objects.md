# Process handles

Use a process handle when an application needs an explicit query, wait or
termination grant that can be duplicated, inherited or kept after collection.
Include `<rum/user.h>` and link with the normal rum SDK runtime.

PIDs identify processes for discovery and the existing parent/child API.
`rum_process_open` checks authority and returns an unsigned typed-object token;
knowing a PID does not grant access. The token refers to that process instance,
even after its registry slot is reused. It is not a PID or stream descriptor.

## Open and observe a child

```c
/* pid was returned by rum_spawn. */
rum_object_handle_t process;
rum_result_t error = rum_process_open(pid, RUM_RIGHT_QUERY | RUM_RIGHT_WAIT,
                                    0, &process);
if (error < 0) return error;
struct rum_process_object_information info = {
    .version = RUM_PROCESS_OBJECT_VERSION, .size = sizeof info
};
rum_result_t ready = rum_process_wait(process, 0, &info);
if (ready == 1) {
    /* info.termination, info.status and info.fault_vector describe the exit.
       This observation leaves the parent's rum_wait result available. */
}
rum_object_close(process);
```

The parent still collects its child once with `rum_wait`, or through the existing
foreground/job collector. A typed wait does not perform that collection.

## Acquisition policy

| Target | Rights an open may request |
| --- | --- |
| Self | QUERY and DUPLICATE |
| Direct child, including an exited uncollected child | QUERY, DUPLICATE, WAIT and TERMINATE |
| Sibling under the existing foreground shell-command delegation | Same process rights, with `RUM_PROCESS_OPEN_SESSION` |
| Unrelated process, another session, protected shell or adopted orphan | Access denied |

SESSION requires the current process to be a marked foreground command of the
target's shell. A flag, executable name or path cannot establish that role.
Ordinary programs cannot open arbitrary process handles. The initial shell can
manage its own children through the direct-child rule. Self inspection is
allowed, including for the shell; self opens cannot grant WAIT or TERMINATE.

Rights are explicit. QUERY returns a process snapshot, WAIT observes completion,
and TERMINATE requests cancellation. DUPLICATE is needed to create another typed
alias; it does not imply query or management rights. Unknown or inappropriate
rights are rejected. A valid close needs no extra right.

`RUM_PROCESS_OPEN_INHERIT` opts the new entry into child inheritance. It is
distinct from SESSION and can be combined with it. Otherwise the handle is
non-inheritable. Existing `rum_object_duplicate` can preserve or reduce rights
and opt an alias into inheritance with `RUM_OBJECT_INHERIT`.

Use [per-launch grants](handle-inheritance.md) to select an eligible process
handle and reduce its rights for a specific child without making another alias.
A WAIT-only child grant cannot query or terminate the target. Child flags may
stop automatic onward inheritance without changing the parent's entry.

Authority is checked at acquisition and each operation checks the entry's grant.
An explicitly inherited handle delegates those rights without requiring the
recipient to be the original parent or shell command. Closing the original
entry does not revoke aliases. No revocation mechanism is provided.

Legacy `rum_terminate` continues to check its parent/session policy on every
PID request. Legacy waits remain restricted to direct children. They use the
same relationship policy as handle acquisition and retain their existing ABI.

## Query, wait and terminate

`rum_process_query(handle, &info)` requires QUERY and returns zero with a
sanitized snapshot: PID, captured parent PID, name, public state, cancellation
flag and completed exit information. Initialize version/size and zero reserved
words. Reusing a successful output packet is supported. There are no page-table,
kernel-stack, fault-address or instruction-pointer fields.

The parent PID is captured when the first process reference is opened; zero
denotes a kernel parent. A live query reports current scheduling/cancellation
state. Once exit is published, status is immutable. Faults expose only the
public exception vector.

`rum_process_wait(handle, flags, &info)` requires WAIT. It returns 1 with completed
status, 0 for a pending `RUM_PROCESS_OBJECT_NOHANG` request, or a negative error.
Zero flags blocks. Pending and failed waits leave output unchanged. Waits are
non-consuming: different aliases and inherited observers can retrieve the same
completion repeatedly, before or after the parent collects its own result.

`rum_process_terminate(handle)` requires TERMINATE and requests cancellation at
a safe boundary. It does not wait or forcibly preempt kernel code. Repeating
the request, including after exit and collection, succeeds without targeting a
new process with the same displayed PID. Synchronous bounded kernel I/O can
delay termination. Close alone never terminates a process.

## Lifetime and limits

The first authorized open creates a shared process-reference object in a bounded
128-record kernel pool. Later opens and aliases reuse it. Each live registry
record owns a runtime reference; each handle owns a TABLE reference, and each
active query, wait or termination owns an OPERATION reference.

On exit, file handles, typed entries, pending I/O and user-buffer leases are
released before completion is published. A process-handle wait owns its own
event and operation reference. Cancelling that waiter releases the reference
once; it does not consume or cancel the target unless termination rights are
separately exercised.

Normal parent/job/orphan collection releases execution resources: address space,
kernel stack, working directory, console membership and registry slot. Retained
handles preserve only the small status object. They neither pin those execution
resources nor steal a parent's result. A completed child that has not been
collected still occupies its ordinary task slot, as with the legacy API.

The runtime reference is dropped when collection detaches the record. If that
would invoke final destruction inside a protected reaper update, cleanup is
deferred to a safe task boundary. Final close after detachment releases the
remaining status storage. Reference objects count in common object diagnostics;
they do not appear as live processes after collection.

The process pool can return ENOMEM independently of the 16-process registry
limit and the 32-entry typed-handle table. A full handle table returns EMFILE.
Failed opens and partial child construction roll back acquired references.

## Public packets

Syscall 38 (`RUM_SYS_PROCESS_OBJECT`) takes a writable 48-byte
`rum_process_object_request` in EBX; ECX and EDX must be zero. Definitions are in
`sdk/include/rum/abi/process-object.h`. Set the exact version/size and zero all
reserved, unused and output-only fields.

| Operation | Inputs beyond version/size | Output |
| --- | --- | --- |
| OPEN | process_id, rights, flags (SESSION and/or INHERIT) | result_handle |
| QUERY | handle, data | 112-byte process information |
| WAIT | handle, data, flags (zero or NOHANG) | Information on completion only |
| TERMINATE | handle | None |

QUERY/WAIT output storage must be readable and writable across its entire range,
with initialized version/size and zero reserved words. It cannot overlap the
control packet. The kernel validates both packets before touching ownership or
blocking, and retains their mapping lease until completion or cancellation.

Malformed packets return EINVAL, invalid memory EFAULT, invalid/stale tokens
EBADF, wrong object types EINVAL, missing grants or relationships EACCES, and
unknown/reaped PIDs ESRCH at open. Blocked cancellation unwinds with EINTR before
the caller's process termination. An unsupported process-object implementation
returns ENOSYS. Older syscall numbers/layouts are unchanged; released 0.7
kernels return ENOSYS for call 38.

## Focused checks

```powershell
.\rum.ps1 test -Suite process-objects -Jobs 4
```

```sh
./rum test --suite process-objects -j4
make test-process-objects-host
```

Host checks cover retained results, non-consuming waits, cancellation, deferred
destruction and pool exhaustion/reuse. Ring-3 checks run through ELF/16 MiB and
ISO/64 MiB: denied acquisition, reduced inheritance, cross-page packets, handle
exhaustion, constructor failure, old/new identity separation, child faults,
pending pipe I/O and cancellation of process waits. A private fixture deliberately
reuses PIDs and execution IDs; production IDs remain monotonic and do not wrap.
Repeated cycles restore object, descriptor, pipe, heap and page counts.

Legacy lifecycle checks include positive shell delegation and foreign-session
denials. The SDK group exercises process observation and termination from an
external project while file handles and private memory remain active.
