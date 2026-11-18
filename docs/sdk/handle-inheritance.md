# Choosing a child's handles

Use `rum_spawn_with_objects` to choose a child's typed handles alongside its
stream-descriptor mappings. The child receives only the listed typed entries,
with the rights and inheritance flags requested for that launch.

```c
/* fd is an existing readable file descriptor. */
rum_object_handle_t source;
rum_result_t error = rum_object_from_descriptor(fd, RUM_OBJECT_FILE,
    RUM_RIGHT_READ | RUM_RIGHT_QUERY, RUM_OBJECT_INHERIT, &source);
if (error < 0) return error;
struct rum_object_inheritance grant = {
    .handle = source, .type = RUM_OBJECT_FILE, .rights = RUM_RIGHT_READ
};
struct rum_handle_mapping input = { .source = fd, .target = RUM_STDIN };
rum_result_t pid = rum_spawn_with_objects("/APP.ELF", &arguments,
    &input, 1, 0, &grant, 1);
rum_object_close(source);
/* On success the child owns both references. Its typed token equals source;
   pass that opaque value in arguments when the program needs to identify it. */
```

The parent can close its entry after launch without invalidating the child's
reference. Stream descriptors and typed tokens are separate namespaces, even
when they refer to the same underlying resource.

## Selection and access rules

Each `rum_object_inheritance` contains a source token, expected type (or ANY),
rights, flags and two zero reserved words. At most 32 entries are accepted.

- The source entry must carry `RUM_OBJECT_INHERIT`. Selecting an entry does not
  override that opt-in.
- Requested rights must be a type-valid subset of the source entry's rights.
  Zero rights are allowed; the child can close that entry but cannot operate on it.
- A token can appear only once. Wrong types, unknown bits and nonzero reserved
  words are rejected before references are acquired.
- The child's token keeps the same numeric value. All source generation history,
  including omitted and retired slots, is copied so later child opens cannot
  accidentally validate an old token.
- Child flags are zero or `RUM_OBJECT_INHERIT`. Zero stops automatic onward
  inheritance for that entry. It does not revoke another alias's rights.
- Explicit inheritance requires the source's inheritance opt-in, not DUPLICATE.
  Creating another alias in the same table still requires DUPLICATE.

An empty list (`NULL, 0`) inherits no typed handles, including entries otherwise
marked inheritable. Unlisted entries are omitted. This supports a narrow grant
without changing the parent's entries or creating temporary reduced aliases.

## Rules by resource

| Resource | What duplicates and inherited references share | Authority remains limited by |
| --- | --- | --- |
| Regular file | Backend identity, access mode, offset, append mode, deferred error | Entry rights and verified open access |
| Directory | Backend identity and iteration cursor | ENUMERATE; directory-only operations |
| Pipe reader/writer | The same endpoint and bounded buffer | Direction-specific READ or WRITE |
| Console input/output | The same console stream description | Direction plus current console/session ownership |
| Process | The same process instance and retained completion status | QUERY, WAIT or TERMINATE granted at acquisition |

Reducing typed rights does not alter the shared description's access mode or
restrict an independently mapped descriptor. For example, a read-only typed
alias does not make a separately granted read/write descriptor read-only.
Grant only the descriptor mappings the child should actually use.

Console handles grant no foreground, shell-command or job-management role.
Process handles can deliberately delegate their rights to a non-parent; a
recipient cannot add query, wait or termination rights it did not receive.
Closing a process reference does not terminate its target, and process waits
do not consume the parent's separate collection result.

## Stream compatibility

Descriptor mappings retain the [existing rules](shared-handles.md): named
sources, unique targets, direction checks on 0–2, and rejection of CLOSPAWN
sources. Unmapped standard streams default to fresh console descriptions unless
`RUM_SPAWN_NO_DEFAULT_STREAMS` is set. Other unmapped descriptors are closed.

PIPELINE and JOB flags keep their existing shell-scope checks. An object list
does not establish those roles or change console ownership.

Existing `rum_spawn`, foreground RUN and `rum_spawn_with_handles` calls retain
automatic typed inheritance: only entries marked INHERIT are copied, with their
current rights and flags. Version-1 mapped spawn still uses its original 32-byte
packet. The new wrapper uses explicit selection instead of that automatic copy.

CLOSPAWN and INHERIT are per-entry policies. A descriptor marked CLOSPAWN cannot
be directly exported as inheritable, but changing that descriptor's flag does
not revoke an already authorized typed alias. An alias with DUPLICATE may create
another inheritable alias within its rights. There is no revocation API.

## Publication and failure

The kernel copies the complete spawn packet, both bounded mapping arrays, path
and argument packet before loading the executable. It validates grants against
the current source table, then prepares private descriptor and object tables.
All references stay owned by the caller's staging tables until publication.

The constructor moves both complete tables into the child in one protected
publication update, after the address space, kernel stack, console preflight and
cancellation check succeed. Failure leaves no child-visible partial table.
Private copies are released on invalid mappings, reference exhaustion, load
errors, allocation failure or cancellation. Trusted producers keep ownership of
supplied private tables when the constructor itself fails.

Parent entries, child entries and acquired operations own independent references.
Closing one cannot invalidate the others. A blocked I/O operation retains its
object until completion or cancellation unwinds. Final close performs backend
cleanup once; it does not promise rollback of already committed file writes.

The existing policy still cancels descendants when their owning process/session
ends. That is process lifecycle policy, not revocation of a shared object:
remaining legitimate references preserve lifetime and completed status.

## Packet and errors

Syscall 25 accepts the existing version-1 packet or the 64-byte version-2
`rum_spawn_objects_request`. Its `streams` prefix contains the original fields,
with version `RUM_SPAWN_OBJECTS_VERSION` and size **64**. The suffix is the object
array's 32-bit address, count and six zero reserved words. Each grant is 24 bytes.
Zero counts require zero addresses. Packets and arrays may cross readable user
pages; they are input-only. Counts are bounded before multiplication or copying.

Success returns a positive PID. Bad packets, duplicate entries, wrong types and
unknown rights return EINVAL; inaccessible input returns EFAULT; invalid/stale
tokens return EBADF; non-inheritable sources or increased rights return EACCES.
Reference saturation returns EOVERFLOW; resource exhaustion returns ENOMEM or
the existing descriptor-limit error. Failure leaves source entries unchanged.

The SDK advertises `spawn-object-grants-v1`. Older kernels that implement syscall
25 only at version 1 reject the new packet with EINVAL. Existing syscall numbers,
descriptor packets and released 0.7 SDK binaries remain compatible.

## Focused checks

```powershell
.\rum.ps1 test -Suite handles -Jobs 4
.\rum.ps1 test -Suite object-handles -Jobs 4
.\rum.ps1 test -Suite process-objects -Jobs 4
```

Host checks cover every type, reduced rights, omitted/private entries, source
generation history, full tables and reference-saturation rollback. Guest checks
combine stream mappings with all seven object types, cross-page packets/arrays,
invalid mappings, failed loads, constructor allocation failure and cancellation
after stack allocation but before publication, child faults,
onward-inheritance flags and a full 32-entry child table. Process tests cancel a
waiter holding a reduced explicit grant. Repeated runs restore object, handle,
pipe, filesystem and page counts through ELF/16 MiB and ISO/64 MiB boots.

The SDK test builds an external application that combines a pipe mapping with
an explicit process-query grant while performing FAT I/O and memory workloads.
