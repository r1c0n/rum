# Filesystems and paths

The kernel filesystem API provides path lookup, directory iteration, byte I/O,
and referenced objects through the same operations for RAM and mounted backends.
Include `<rum/fs.h>` for the API and `<rum/path.h>` only when working on path
validation itself.

RAM files occupy `/`. A supported [FAT16 disk](fat16.md) mounts at
`/disk` during boot. `/disk` returns `FS_UNAVAILABLE` without a valid volume.
The existing kernel shell, ELF loader, and Snake continue to use the flat
RAM-file API. User programs can access this namespace through
[filesystem syscalls](filesystem-syscalls.md). The kernel shell has no `cd` command.

## Path rules

Absolute paths start at `/`. Relative paths start at the supplied context's
working directory. A null context means `/` for either kind of path.

| Input | Result |
| --- | --- |
| `//disk//docs/./readme.txt` | `/disk/DOCS/README.TXT` |
| `../ROOT.TXT` from `/disk/DOCS` | `/disk/ROOT.TXT` |
| `..` from `/` | `/` |
| `..` from `/disk` | `FS_MOUNT_ESCAPE` |
| `/notes.txt` from `/disk/DOCS` | RAM file `/notes.txt` |
| `/disk/MISSING/../ROOT.TXT` | `FS_NOT_FOUND` if `MISSING` does not exist |
| `/disk/ROOT.TXT/.` | `FS_NOT_DIRECTORY` if `ROOT.TXT` is a file |

Repeated separators are accepted. Every component is still traversed: `..`
cannot cancel a missing directory or turn a file into a directory. A trailing
separator, `.` or `..` requires a directory. Climbing above the global root
stays at `/`; climbing above a mounted root fails. An absolute path can select
the RAM root directly when a caller is working inside the disk mount.

Before backend lookup, the parser checks the entire path for invalid bytes,
length, depth, and mount escape. It preserves the traversal steps separately
from the canonical spelling. An invalid component is rejected even if a later
`..` would remove it from the final spelling.

Paths are NUL-terminated kernel strings. Accepted name bytes are ASCII letters,
digits, `.`, `_`, and `-`; `/` separates components. Spaces, control bytes,
backslashes, colons, and non-ASCII bytes are rejected. Empty paths are invalid.
The future syscall boundary must copy user strings into bounded kernel storage
before passing them here.

## Limits and names

The public limits live in `include/rum/abi/filesystem.h`. Kernel aliases and
table capacities live in `include/rum/fs_limits.h`.

| Limit | Value |
| --- | --- |
| Raw or canonical path | 255 bytes plus NUL |
| RAM component | 63 bytes plus NUL |
| Names below a mount root | 16, including the final filename |
| Distinct retained objects | 128 across both backends |
| Kernel references | 530 across contexts and open objects |
| RAM files | 64 files, each at most 64 KiB |

The raw input and the resulting relative path must both fit. Temporary traversal
depth also counts: adding a seventeenth component and then `..` is rejected.
The `/disk` prefix is a mount point and does not count toward its backend depth.

RAM names are case-sensitive and retain their original rules, including multiple
dots and leading dots other than the exact special names `.` and `..`. RAM
remains a flat filesystem; it cannot create directories. The disk naming policy
accepts an optional extension with one to eight base characters and one to
three extension characters. A name without an extension has one to eight
characters. Leading dots, trailing dots, and multiple dots are rejected. Input
is folded to uppercase before lookup or mutation, and directory entries use
uppercase spelling. Long FAT names are unsupported.

The mount name is exactly lowercase `disk`. If a legacy RAM file already has
that name, it remains readable and mounting returns `FS_EXISTS`. Remove it
before mounting. Once mounted, the RAM API cannot create a colliding file.
The common root listing includes the `disk` directory entry even while its
backend is unavailable; a colliding legacy file appears instead when present.

## Opening and reading a file

```c
struct fs_context *cwd = task_current_filesystem();
fs_reference file;
enum fs_error error = fs_open(cwd, "/notes.txt", FS_READ, &file);
if (error == FS_OK) {
    unsigned char bytes[128];
    struct fs_io_result result = fs_read(file, 0, bytes, sizeof bytes);
    if (result.error == FS_OK) {
        /* Consume exactly result.transferred bytes; binary data is valid. */
    }
    fs_close(file);
}
```

`fs_open` requires `FS_READ`, `FS_WRITE`, or both. It opens an existing object;
it does not create or truncate it. Directory references support read access
for iteration. Opening a directory for writing returns `FS_IS_DIRECTORY`.
An invalid mode or stale reference returns `FS_INVALID`. Attempting an operation
outside the reference's access mode returns `FS_ACCESS`.

Offsets are explicit 64-bit values. The common layer rejects `offset + bytes`
overflow and nonzero operations with null buffers before backend calls. Zero
byte operations validate the reference, mode, and kind but perform no backend
I/O. Reads return zero transferred bytes at or beyond EOF. Callers must handle
short operations and inspect both the error and transferred-byte count;
an I/O error may follow partial progress. ATA status and error-register bytes
can accompany backend failures in `fs_io_result`.

`fs_stat` refreshes the current size and kind of an open object. `fs_stat_path`
looks up a path without retaining it. Neither gives a borrowed payload pointer.
`fs_duplicate` returns another independently closable reference with the same
identity and access mode. Generic references do not store a current offset;
per-process handle offsets belong to the handle/syscall layer.

## Mutation and object lifetime

Use `fs_replace(cwd, path, data, bytes)` to create a file or replace its complete
contents. `fs_write(reference, offset, data, bytes)` writes an already open file.
`fs_truncate(reference, size)` resizes it through a write reference, filling new
bytes with zeroes. Both RAM and FAT16 support it with their usual size limits.
`fs_mkdir` creates a directory where supported. `fs_remove` removes a file or
an empty directory. Missing parents are never created automatically.

An identity consists of the mount, its generation, and the backend's object ID.
All references to that identity share one retained object, regardless of path
spelling or aliases. Removal and whole-file replacement return `FS_BUSY` while
that object is open. RAM's legacy `ramfs_put` and `ramfs_remove` honor the same
retention rule. Ordinary byte writes and resizing through a writable reference
are allowed; other references observe its updated size and contents.

RAM replacement and growing writes allocate and copy before changing ownership.
Allocation failure preserves the previous bytes and size. Writes can overwrite
or append up to 64 KiB but cannot leave a hole beyond the current end. Replacing
or removing a file after all references close is allowed. Recreated RAM files
receive a new ID, and closed reference tokens are never reused.

Mount roots cannot be removed or replaced. Unmounting returns `FS_BUSY` while
any disk file, directory, or working directory is retained. A later mount gets
a new generation even if its backend returns the same object IDs. Unmounting
does not flush automatically: complete writes, call `fs_flush`, close references,
then unmount. `fs_flush` reports backend failure; RAM needs no disk flush.

Backend write failures and interrupted-write guarantees belong to the backend.
The common API does not promise atomic disk replacement or crash recovery.

## Directories and working directories

Open a directory with `FS_READ`, start a `uint64_t` cursor at zero, and call
`fs_readdir(reference, &cursor, &entry)` until it returns `FS_END`. An entry
contains its name, kind, and size. Cursors advance only on a successful entry;
errors and EOF leave the cursor unchanged. Do not interpret a cursor as an
array index. Restart iteration after namespace mutation or mount changes.

Initialize independent contexts with `fs_context_initialize`, clone them with
`fs_context_clone`, change directories with `fs_context_chdir`, and release them
with `fs_context_destroy`. Initialize or clone into a zeroed context. A failed
directory change leaves both its old reference and path intact. Do not edit
the context's path or token by hand.

The boot task starts at `/`. Workers and user processes inherit independent
references to their creator's directory at publication. Changing a child's
directory does not change its parent. Exited processes keep their directory
until reaped, including after a user fault or cancellation. Failed construction
releases the temporary directory reference without taking the caller's address
space. Idle owns no working directory.

`task_current_filesystem()` gives foreground kernel code its current context.
`task_working_directory(id, buffer, capacity)` provides a bounded, IRQ-safe copy
for inspection. The filesystem operations themselves are for boot or foreground
task context, never IRQ handlers.

## Implementing a backend

Supply `fs_operations` and a `fs_backend` descriptor. The disk registration
function, `fs_mount_disk`, requires the FAT 8.3 naming policy, a nonzero directory
root, and lookup, stat, retain, release, read, and readdir callbacks. Write,
replace, remove, mkdir, truncate, and flush may be omitted. Missing mutation
callbacks return `FS_UNSUPPORTED`; a read-only descriptor returns `FS_READ_ONLY`.
Read-only backends without a flush operation need no flush and return success.
An optional `unmount` callback releases backend-owned caches after all pins have
closed and the namespace has detached the mount. It must not reenter the
filesystem API or perform disk I/O.

Lookup receives a validated, normalized single component and parent ID.
Stat returns the same ID requested. IDs must be nonzero and stable while
retained; every name referring to one object must return the same ID. Lookup,
stat, and readdir do not acquire references. Retain/release pin the identity and
must not block, allocate, or perform disk I/O, because task reaping releases
directory pins with interrupts disabled. All mutation entry points must honor
those pins, including backend-specific callers outside the common layer.

Callbacks execute serially. The common layer uses short interrupt-protected
updates for ownership publication and preserves the caller's IF state. It does
not mask interrupts across disk I/O. A reentrant operation returns `FS_BUSY`
without spinning; callbacks must not yield to another task or call back into
the namespace API. Backend storage and callback tables must remain valid until
unmounted. The descriptor is copied on registration.

Read/write callbacks must never report more transferred bytes than requested.
Readdir returns `FS_END` without an entry at EOF and a strictly advancing opaque
cursor for each success. Do not emit `.` or `..` entries; the path layer handles
those itself. Names and kinds returned by a backend are checked
before publication. A writable backend without a flush callback returns
`FS_UNSUPPORTED` for flush.

| Error | Meaning |
| --- | --- |
| `FS_NOT_FOUND`, `FS_NOT_DIRECTORY`, `FS_IS_DIRECTORY` | Lookup failure or wrong object kind |
| `FS_EXISTS`, `FS_NOT_EMPTY`, `FS_BUSY` | Mutation conflicts or retained ownership |
| `FS_INVALID`, `FS_PATH_TOO_LONG`, `FS_NAME_TOO_LONG`, `FS_TOO_DEEP` | Invalid arguments or path limits |
| `FS_MOUNT_ESCAPE`, `FS_RANGE` | Mounted-root traversal or byte-range violation |
| `FS_ACCESS`, `FS_READ_ONLY`, `FS_UNSUPPORTED` | Access mode or backend capability |
| `FS_NO_MEMORY`, `FS_NO_SPACE` | Allocation, media, or fixed-table exhaustion |
| `FS_UNAVAILABLE` | Backend or storage unavailable |
| `FS_IO_ERROR`, `FS_TIMEOUT`, `FS_DEVICE_FAULT` | Storage failure |

Use `fs_error_from_block` to translate block-device failures. Preserve the
block result's transferred-byte count and device diagnostics when constructing
an I/O result. `fs_error_name` provides readable kernel diagnostic text.

## Testing changes

Run `make test-fs` for parser tests, common-layer host tests, and isolated QEMU
fixtures at 16 and 64 MiB. The fixtures mount an in-memory directory backend
with FAT naming; they do not validate FAT sectors. They check traversal, bytes,
access, aliases, mount boundaries, table exhaustion, allocation rollback, and
working-directory ownership through actual user exit, fault, and cancellation.

Run `make test` before a pull request to also check the existing RAM shell,
embedded ELF programs, Snake, memory protection, raw disks, and release package.
