# Shared handles and child streams

Include `<rum/user.h>` and link with the normal rum runtime. Each process has
32 descriptor slots. A slot references an open description containing the
backend reference, access mode, file/directory position and deferred I/O error.
Descriptor flags belong to the slot itself.

Separate opens have independent positions. Duplicates and explicitly inherited
handles share a description: reading, writing, seeking or iterating through one
alias changes the position seen by every alias. A deferred error is reported
by the next otherwise-valid nonempty transfer through any alias. Zero-length
transfers and seeks do not discard it.

## Duplication

```c
rum_result_t alias = rum_duplicate(file, RUM_DUP_ANY, 0);
if (alias < 0) return 1;
rum_close(file);
/* alias still owns the description. */
rum_close((rum_handle_t)alias);
```

`RUM_DUP_ANY` allocates the lowest free slot in 3–31. An explicit target can
be any slot in 0–31, replacing its previous descriptor on success. Failure
preserves the target. Source equal to target only changes that slot's flags.
`rum_open` still allocates only 3–31, even if a standard slot is closed.

Flags are zero or `RUM_DUP_CLOSPAWN`. A marked slot cannot be used as a child
mapping source (`-RUM_EACCES`). This does not affect other aliases. Clear it
with `rum_duplicate(fd, fd, 0)`. This is an inheritance policy, not an extra
security privilege.

Target 0 requires read access; targets 1 and 2 require write access. Directories
cannot occupy these slots. Duplication preserves the shared access mode: a
read/write file remains read/write even when mapped to 0. Open a separate
read-only description when needed.

Table exhaustion returns `-RUM_EMFILE`. The global pool is bounded by
`RUM_PROCESS_LIMIT * RUM_ABI_HANDLE_LIMIT` (currently 512 descriptions); pool
exhaustion returns `-RUM_ENOMEM`. Aliases need no new description or backend pin.

## Explicit child mappings

`rum_spawn_with_handles(path, arguments, mappings, count, flags)` returns a
positive child PID. It starts a nonforeground child; collect it with `rum_wait`.

A `rum_handle_mapping` contains parent source, child target and two zero
reserved words. At most 32 mappings are accepted. Sources may repeat; targets
must be unique. Every source is resolved against the original parent table,
so mapping order cannot overwrite another source.

Only named descriptors are inherited. Unmapped 0–2 default to fresh console
streams; other unmapped slots are closed. `RUM_SPAWN_NO_DEFAULT_STREAMS` instead
starts with an empty table and installs only the mappings. Zero count requires
a NULL mapping pointer. Mapped child slots have zero descriptor flags.

Save a program's output to a file:

```c
if (rum_replace("/OUT.TXT", NULL, 0) < 0) return 1;
rum_result_t file = rum_open("/OUT.TXT", RUM_OPEN_WRITE, 0);
if (file < 0) return 1;
struct rum_arguments args = {
    .argc = 1, .string_bytes = sizeof "/RUM/HELLO.ELF",
    .strings = "/RUM/HELLO.ELF"
};
struct rum_handle_mapping output = {
    .source = (rum_handle_t)file, .target = RUM_STDOUT
};
rum_result_t pid = rum_spawn_with_handles("/RUM/HELLO.ELF", &args, &output, 1, 0);
rum_close((rum_handle_t)file);
if (pid < 0) return 1;
struct rum_process_result child = {
    .version = RUM_PROCESS_ABI_VERSION, .size = sizeof child
};
if (rum_wait((rum_pid_t)pid, 0, &child) != 1) return 1;
return child.termination == RUM_PROCESS_EXITED ? child.status : 1;
```

The child keeps the description alive after the parent closes its copy. Writes
use the file backend even at slot 1. Console-input aliases still enforce
foreground ownership; inheritance grants no keyboard or shell-session authority.

Legacy `rum_spawn`, `rum_run` and `rum_run_command` keep their fresh console
streams and do not inherit files. [Pipes](pipes.md) can use these mappings too.
The shell uses these mappings for [redirection and pipelines](shell-workflows.md).
Its foreground scope additionally permits `RUM_SPAWN_PIPELINE`; this flag is
restricted to the initial shell and grants input to the first stage only.

## Public ABI

| Call | Syscall | Packet |
| --- | --- | --- |
| `rum_duplicate` | 24 | 24-byte `rum_duplicate_request`: version, size, source, target, flags, reserved |
| `rum_spawn_with_handles` | 25 | 32-byte `rum_spawn_mapped_request`: version, size, path, arguments, mappings, mapping_count, flags, reserved |

Duplication uses `RUM_HANDLE_ABI_VERSION`; mapped spawn uses
`RUM_SPAWN_MAPPED_VERSION`, both currently 1. Each mapping is 16 bytes. Addresses
and handles are 32-bit values. Existing packets and syscall numbers remain
compatible.

The kernel copies and validates complete packets, cross-page arrays, paths and
arguments before executable loading. Invalid mappings reach no backend.
Unknown fields/conflicts return `EINVAL`, bad user ranges `EFAULT`, missing
sources or wrong access direction `EBADF`, standard directory mappings `EISDIR`,
and CLOSPAWN sources `EACCES`, using negative `RUM_*` results.

## Ownership and cleanup

Failed launch releases its private table and loaded address space. The task
constructor moves a prepared table only when the child is completely published.
Exit, fault, cancellation and orphan cleanup close every descriptor. The final
alias releases the backend reference; removal and whole-file replacement remain
busy while any alias retains the object.

Offsets/errors update in cooperative kernel task context. Synchronous file
callbacks do not yield, and IRQ returns to kernel mode do not preempt them.
This does not provide synchronization for future SMP or multiple user threads.

Recovery `diag` shows descriptor references, open descriptions and aliases
(references minus descriptions). Counts include private launch tables. All
return to zero after userspace and its children are reclaimed.

## Focused checks

```powershell
.\rum.ps1 test -Suite handles -Jobs 4
```

```sh
./rum test --suite handles -j4
make test-shared-handles-host  # Ownership checks without QEMU.
```

Host checks cover shared positions/errors, directory cursors, mode checks,
forbidden contexts, exhaustion and partial pool rollback. Ring-3 checks use
ELF/16 MiB and ISO/64 MiB boots for mapped streams, cross-page packets, validation
before backend calls, failed loads/construction, parent exit, child fault,
cancellation and resource baselines. Logs are saved under
`build/test-artifacts/shared-handles/`.
