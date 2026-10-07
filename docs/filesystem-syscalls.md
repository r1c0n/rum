# Files from user programs

Include `<rum/user.h>` and link with the normal rum user runtime. Programs can
open existing RAM files and files on a mounted FAT16 volume, read or overwrite
their contents, list directories, change their working directory, create
directories, remove closed objects, and flush a backend.

The normal [userspace shell](shell.md) uses these calls on RAM, `/rum` and `/disk`. The
kernel recovery shell keeps its original RAM commands.

## Opening and reading

```c
rum_result_t result = rum_open("/disk/DOCS/NOTE.TXT", RUM_OPEN_READ, 0);
if (result < 0) return 1;
rum_handle_t file = (rum_handle_t)result;
char buffer[512];
for (;;) {
    result = rum_read(file, buffer, sizeof buffer);
    if (result <= 0) break;
    /* Consume exactly result bytes; file contents need not be text. */
}
rum_result_t closed = rum_close(file);
return result < 0 || closed < 0;
```

`rum_open` accepts `RUM_OPEN_READ`, `RUM_OPEN_WRITE`, or their bitwise OR.
Flags are zero or `RUM_OPEN_DIRECTORY`, which requires read-only access and
rejects regular files. With zero flags, a read-only open may also open a
directory. Directory handles support `rum_readdir`, not byte reads or writes.
Unknown bits, zero access, or a writable directory request are rejected.

Open does not create or truncate a file. `rum_replace(path, data, bytes)` creates
or replaces a whole file, up to 65,536 bytes. Zero bytes creates an empty file
and ignores the data pointer. An open file identity cannot be replaced. A
writable handle can overwrite or append to an
existing file, subject to RAM or FAT limits. A write beyond EOF returns
`-RUM_ERANGE`; sparse files are unsupported.

`rum_replace` uses syscall 14 and a 24-byte `rum_replace_request`: version,
size, path address, data address, byte count, and a zero reserved word. It
checks the full readable data range and copies it into kernel storage before
calling the backend. Success returns zero. Unknown version/size/reserved fields
return `-RUM_EINVAL`; an oversized request returns `-RUM_E2BIG`. Disk failures
may follow committed metadata; the [FAT16 interrupted-write guarantee](fat16.md)
still applies.

Each process starts with input, output and error at handles 0, 1 and 2.
File and directory handles occupy 3–31. The first free slot is used; exhausting
these 29 slots returns `-RUM_EMFILE`. Closing a stream is allowed, but its number
is never allocated to a file. File handle numbers may be reused after close;
do not keep using a saved number once you close it. Child processes start with
fresh streams and do not inherit the parent's open files.

## Transfers, offsets and errors

File I/O transfers at most 512 bytes per syscall; console I/O transfers at most
128. Callers must loop for longer operations. A positive result is the exact
number of bytes transferred and advances that handle's offset by that amount.
A file read at or beyond EOF returns zero. A zero-length operation ignores its
buffer after validating the handle, direction and object kind.

The kernel validates the entire nonempty user buffer, including pages beyond
the chunk it will transfer. Invalid, unmapped, supervisor-only, overflowing or
read-only output ranges return `-RUM_EFAULT` before any backend call or offset
change. Paths are copied into bounded kernel storage and parsed before lookup.
No driver receives a user pointer.

If a backend completes a prefix and also reports an error, the syscall returns
the positive prefix count. The handle remembers the error and returns it on the
next otherwise-valid nonempty read or write without doing more I/O. Zero-length
calls and seeks do not discard that error. This allows callers to account for
bytes that were already committed before observing the failure. A negative
I/O result never advances the offset.

`rum_seek(file, displacement, whence, &position)` uses a signed 64-bit
displacement. `RUM_SEEK_SET` starts at zero, `RUM_SEEK_CUR` at the current
offset, and `RUM_SEEK_END` at the current file size. Position is an unsigned
64-bit result; pass `NULL` if the wrapper need not return it. Underflow, overflow
and `offset + requested_bytes` overflow return `-RUM_EOVERFLOW`. Seeking beyond
EOF is allowed, but does not grow the file. Streams cannot seek. A directory
supports only `rum_seek(directory, 0, RUM_SEEK_SET, NULL)` to restart iteration.

## Directories and paths

```c
rum_result_t opened = rum_open("/disk/DOCS", RUM_OPEN_READ, RUM_OPEN_DIRECTORY);
if (opened < 0) return 1;
struct rum_directory_entry entry = {
    .version = RUM_FS_ABI_VERSION, .size = sizeof entry
};
rum_result_t result;
while ((result = rum_readdir((rum_handle_t)opened, &entry)) == 1) {
    /* entry.name is NUL-terminated; entry.kind identifies file or directory.
       Size is ((uint64_t)entry.size_hi << 32) | entry.size_lo. */
}
rum_close((rum_handle_t)opened);
return result < 0;
```

Initialize the entry packet once with its version, size and a zero reserved
word. Output fields may be reused between calls. Readdir returns 1 for an entry,
0 for the end, or a negative error. EOF clears the output fields. Errors leave
the packet and cursor unchanged. Restart iteration after directory mutation.
The cursor belongs to the handle and is opaque.

`rum_chdir(path)` changes only the calling process's directory. Relative paths
start there; absolute paths start at `/`. `rum_getcwd(buffer, capacity)` returns
the canonical path and a byte count including its NUL. Insufficient capacity
returns `-RUM_ERANGE` without changing the buffer. A failed chdir preserves the
previous directory. Processes inherit their creator's directory independently.

The same [path rules](filesystems.md#path-rules) apply to every filesystem call:
255 path bytes, 63 component bytes and 16 names below a mount root. FAT names
use uppercase-compatible 8.3 spelling. Repeated separators and `.` are accepted;
`..` cannot escape `/disk`. Use an absolute `/` path to return to RAM from disk.
No valid disk means paths inside `/disk` return `-RUM_ENODEV`.

`rum_mkdir(path)` creates one disk directory; missing parents are not created.
`rum_remove(path)` deletes a regular file or an empty directory. Removal or
whole-file replacement of an open identity returns `-RUM_EBUSY`, including
aliases, directory handles and working directories. Byte writes through an
open handle remain allowed. Close the handles and leave a directory before
removing it. RAM supports files in its flat root, but cannot create directories.

## Wire packets and result codes

`int 0x80` retains the [register convention](user-abi.md#syscall-convention).
The C wrappers construct open and seek packets. Directory iteration accepts a
caller-initialized entry packet. All packet words are `uint32_t`, with exact
layouts declared in `include/rum/abi/filesystem.h`:

| Packet | Bytes | Words in order |
| --- | --- | --- |
| `rum_open_request` | 24 | version, size, path address, access, flags, reserved |
| `rum_seek_request` | 32 | version, size, displacement low/high, whence, reserved, position low/high |
| `rum_directory_entry` | 88 | version, size, kind, file size low/high, reserved, then 64 name bytes |

Version is `RUM_FS_ABI_VERSION` (1), size must exactly match, and reserved words
must be zero. Seek's position fields must be zero on input; its displacement
is two's-complement high/low words. A seek packet must be readable and writable
in its entirety before the offset can change. Directory packets likewise need
read/write access. Output fields other than directory version/size/reserved
are not input flags. Extensions require a new packet version.

Results use rum-specific negative `RUM_E*` codes, without setting `errno`:

| Codes | Meaning |
| --- | --- |
| `EBADF`, `EINVAL`, `EFAULT` | Invalid/closed/wrong-direction handle, invalid arguments, inaccessible user memory |
| `ENOENT`, `ENOTDIR`, `EISDIR` | Missing path or wrong object kind |
| `EEXIST`, `ENOTEMPTY`, `EBUSY` | Namespace conflict, nonempty directory, retained object or disallowed execution context |
| `ENAMETOOLONG`, `EOVERFLOW`, `ERANGE` | Path/depth limit, arithmetic overflow, buffer capacity or backend byte limit |
| `EACCES`, `EROFS`, `ENOSYS` | Backend access restriction, read-only mount, unsupported operation |
| `ENOMEM`, `ENOSPC`, `EMFILE` | Heap exhaustion, backend/global table capacity, process handle capacity |
| `ENODEV`, `EIO`, `ETIMEDOUT` | Missing mount, I/O/device fault, bounded hardware timeout |

## Flush and cleanup

`rum_flush(handle)` invokes the backend's flush operation and reports failure.
RAM needs no physical flush. FAT writes already follow the documented
[data/FAT/directory ordering](fat16.md); an explicit flush does not make the
volume journaled or give general crash recovery. Close releases a reference
without flushing or retrying an uncertain write. Follow FAT's repair guidance
if an I/O error faults the mount.

Exit, user fault and cancellation close every handle before the process is
published as exited. The parent later reaps its working-directory reference,
address space and stack. Failed process construction publishes no handles and
releases its temporary directory reference. Filesystem callbacks run in task
context with interrupts enabled; IRQ handlers only request cancellation or wake
tasks. Callbacks never run inside handle-publication updates.

Run `make test-file-syscalls` for real ring-3 tests at 16 and 64 MiB, including
invalid packets, cross-page buffers, 64-bit seek boundaries, partial-error
delivery, 29-file exhaustion, open-object removal and repeated exit/fault/cancel
resource baselines. The test also mounts host-built FAT16 images and compares
exact bytes and allocation ownership after QEMU exits. It creates disposable
images under temporary directories and never uses your attached working disk.
