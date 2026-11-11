# System files and trusted operations

`/RUM` holds installed system programs. User programs can read and execute those
files, but cannot create, overwrite, append, truncate, remove or rename anything
in that tree. The root name `RUM` remains reserved even when the directory is
missing. Other directories and ordinary root files remain writable, subject to
the filesystem's existing access, retention and read-only-volume rules.

## Program and system authority

| Context | System-file changes | Process and console management |
| --- | --- | --- |
| Kernel boot, recovery and trusted kernel workers | Allowed through kernel services | Trusted kernel operations |
| Initial userspace shell | Denied | Explicit initial-shell/session role and direct children |
| Foreground shell command | Denied | Its own children and existing explicit command delegation |
| Ordinary application | Denied | Its own children or explicitly granted process handles |

The task manager assigns these roles when it publishes a process. Names, paths,
arguments and public request flags cannot create a trusted role. Running a file
called `SHELL.ELF`, placing an ELF in `/RUM`, or launching it from recovery does
not make it a kernel task or grant system-write authority.

There is no userspace administrator flag, privilege-elevation syscall or raw
disk/device handle. Process query, wait and termination still require the
[existing relationship policy or process-handle grants](process-objects.md).
Console operations keep their foreground/session checks. Raw port I/O remains
privileged and faults in user mode.

## Writable application files

Use the normal workspace tools and keep application data outside `/RUM`:

```text
setup
cp /RUM/HELLO.ELF /APPS/HELLO.ELF
run /APPS/HELLO.ELF
write /HOME/NOTES.TXT an island of our own
```

`setup` creates `/HOME` and `/APPS`. Applications may create other data
directories with `mkdir`. Root files such as `/NOTES.TXT` and `/SNAKE.DAT`
remain supported. `/RUM1.TXT` is an ordinary filename; protection applies to
the `RUM` component and its actual object tree, not every name with that prefix.

Programs use the same SDK filesystem calls in every location. EACCES means
the requested operation lacks authority; opening a protected file for reading
still succeeds. Existing open-object busy checks and FAT limits also apply.

## Install or update a system program

Stage a built program in a writable application directory while QEMU is stopped,
or copy an existing program there from the guest. Keep a backup of a system
program before replacing it. Enter recovery from the interactive shell:

```text
recovery
install /APPS/HELLO.ELF /RUM/HELLO.ELF
```

Recovery is a kernel prompt reached after the userspace shell and its processes
have been reclaimed. Its `install` command streams a regular source file into a
destination below `/RUM`, creates `/RUM` if needed, flushes the destination and
closes both references. Source and destination must be different identities,
including through case aliases. Nested destination directories must already
exist; recovery also provides `mkdir`.

The copy supports files up to 16 MiB, including files larger than the 64 KiB
whole-file replacement syscall limit. It copies bytes; it does not sign a
program, validate its ELF contents or confer a new runtime role. Reboot the VM
after a successful install to resume the normal initial shell.

Installation is not atomic replacement. A read, write, full-media or flush
failure can leave a truncated or partially written destination. Preserve the
source and backup, then retry from recovery or repair the image on the host.
The existing [FAT16 interrupted-write guarantees](fat16.md) still apply.
Normal boot never formats the image or installs files automatically.

Recovery also retains its existing file commands, diagnostics and Snake.
Without a disk, those kernel diagnostics and Snake remain available; disk
installation requires a readable, writable mounted volume. A damaged system
shell can be replaced from another directory on a valid volume. Unsupported or
damaged volumes may require offline host repair.

## Paths, identities and retained handles

The common filesystem service protects both normalized paths and backend
identities. Case changes, repeated separators, `.`/`..`, relative paths,
directory aliases and file aliases cannot turn a protected object into an
ordinary writable file. Rename checks both parent locations and both affected
object identities before calling a mutating backend operation.

At mount, a bounded index records the system tree's identities. Trusted namespace
changes rebuild that index, including after an operation that may have partially
changed metadata. An incomplete or invalid index fails closed: ordinary
mutations are denied across the volume until trusted maintenance rebuilds a
valid index. Reads and kernel recovery remain available. The private bound is
512 system identities/entries, with the existing 16-level directory limit.

Open filesystem objects retain their system classification. A writable handle
opened by the kernel does not give an application permission to write a system
file. The filesystem rechecks the current context on write, append and truncate;
legacy descriptor and typed-handle operations apply the same ceiling. Denied
writes do not advance offsets or consume a deferred I/O error.

Typed duplication, export and inheritance also check that ceiling before
granting WRITE. A read-only derived grant remains usable. Automatic inheritance
of a protected writable grant is denied; use an explicit reduced child grant
when the child should read it. Supplied kernel producer tables can retain their
declared access bits, but those bits do not override the child's effective
authority. Closing and cleanup remain permitted regardless of write authority.

Mount/unmount control is kernel-only. Block wrappers reject raw requests from a
user execution context before acquiring a device or invoking its driver. Checked
filesystem reads, writes and flushes reach the block layer within the serialized
filesystem operation, preserving ordinary application I/O.

## Scope

This is a single-user policy, not user accounts, per-user ownership, a general
ACL system or verified boot. The operator controls the physical recovery console
and offline disk contents. Host edits are outside the guest trust model.
Kernel code and filesystem/device backends remain trusted. The current I/O
implementation is synchronous; future worker-based services must preserve the
requester's authority instead of treating a worker's kernel context as a grant.

Existing syscall numbers and layouts are unchanged. The SDK advertises
`system-authority-v1`; older binaries can continue using ordinary application
files, but attempts to modify `/RUM` now return EACCES.

## Focused checks

```powershell
.\rum.ps1 test -Suite authority -Jobs 4
```

```sh
./rum test --suite authority -j4
make test-authority-host
```

Host checks cover alias identities, normalized paths, retained writable handles,
grant reduction/denial, index bounds, malformed cursors, fail-closed behavior,
raw-device denial and trusted installation. Ring-3 checks give both a real
initial shell and an ordinary ELF named `SHELL.ELF` the same retained system
handles, then verify denied writes, explicit roles, port-I/O faults and cleanup.

FAT tests run ELF/16 MiB and ISO/64 MiB boots, install a 65,537-byte file from
recovery and compare exact bytes through guest copy commands and host FAT tools
after a separate boot. Unchanged system files are compared byte for byte and
all live process, handle and object counts return to baseline.
