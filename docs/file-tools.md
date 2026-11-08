# File tools and workspace

`touch`, `cp`, `mv`, `stat` and `setup` are ordinary programs installed in
`/RUM`. They use the public filesystem API and work with absolute paths or
paths relative to the current directory. Files use rum's case-insensitive 8.3
names. None of these commands formats a disk or runs automatically at boot.

## Prepare a workspace

```text
setup
cd /HOME
touch NOTES.TXT
echo an island of our own > NOTES.TXT
cp NOTES.TXT BACKUP.TXT
stat BACKUP.TXT
mv BACKUP.TXT SAVED.TXT
```

`setup` creates `/HOME` and `/APPS` when missing. Existing directories and their
contents are preserved, so it is safe to run again. A regular file at either
path is an error and is never replaced. If the second directory fails, the
first remains; fix the reported problem and rerun the command. Normal boot and
disk preparation do not create this workspace for you.

## Commands

| Command | Behavior |
| --- | --- |
| `touch path` | Create an empty regular file if missing; leave an existing file unchanged |
| `cp [-f] source destination` | Copy a regular file to an exact destination filename |
| `mv [-f] source destination` | Rename a closed regular file within the mounted filesystem |
| `mv [-f] --copy source destination` | Explicitly copy, flush and close successfully, then remove the source |
| `stat path` | Show file/directory type, byte size and volume write mode |
| `sync` | Flush the mounted volume; print an error and return failure if flushing fails |

`cp` and `mv` refuse an existing destination by default. `-f` permits replacing
a closed regular file. Directories cannot be copied, moved or overwritten;
there is no recursive mode or automatic basename selection for a directory
destination. Use `./NAME` for a filename starting with `-`.

Copying a file onto itself is rejected by filesystem identity, including
equivalent case, relative paths and aliases. Renaming a file onto itself is
also rejected. An open source or destination blocks rename. An open destination
blocks forced copy because it cannot be truncated. Copying from an open file
is allowed, and `touch` on an existing open file leaves it intact.

Copies preserve the exact bytes, including binary data, empty files and missing
final newlines. They loop over short reads and writes, use a fixed 512-byte
buffer and check file sizes before creating a destination. Files larger than
the current 32-bit FAT size limit are refused. Copying reports an error if it
observes a source size change; it does not provide a snapshot against concurrent
writers. Keep the source unchanged until the command finishes.

FAT writes stage replacement chains as data is written. Large copies can be
slow and may need free space for both the current and staged destination
chains. A metadata-only rename avoids that copying.

On failure, a partial destination may remain. With `cp -f`, the old destination
was already truncated and cannot be recovered by the tool. A failed explicit
copy move never requests source removal. If copying succeeds but removal fails,
the command reports that stage; both names may remain, or removal metadata may
already have reached disk before an I/O error. These operations have no rollback.

rum currently mounts one FAT16 volume at `/`. Rename reports EXDEV if the common
layer is asked to cross mounts. `mv` requires `--copy` for that operation; it
does not silently replace a rename with copy-and-delete. The explicit form also
works within today's single volume, using the same flush-before-remove rule.

## Timestamps and read-only files

There is no wall clock or public timestamp API yet. `touch` does not update
timestamps on an existing file, even on a read-only volume. Newly created FAT
entries use the filesystem's existing 1980-01-01 placeholder date. Copies get
new destination metadata; renames preserve the source attributes and dates.
`stat` reports timestamps as unavailable. Directory size is the backend's
reported size, currently zero for FAT directories, rather than a recursive total.

`stat`'s write mode describes the volume. Individual FAT read-only attributes
are checked by mutations but are not exposed by this metadata API. A missing
file cannot be touched or copied onto a read-only volume. Reading, metadata
queries and touching an existing regular file remain available.

## Run a copied application

```text
setup
cp /RUM/HELLO.ELF /APPS/HELLO.ELF
/APPS/HELLO.ELF
run /APPS/HELLO.ELF
```

The source program only needs read access. Installing or running a copy under
`/APPS` requires no write to `/RUM`. Build your own applications with the
[rum SDK](user-abi.md), copy their ELFs into `/APPS` with host FAT tools while
the VM is stopped, then use an explicit application path.

Bare names continue to search `/RUM` before the current directory. `/APPS` is
not added to that search path. Use `/APPS/PROGRAM.ELF`, or enter `/APPS` and run
`./PROGRAM.ELF`, to select an application explicitly. The current filesystem
has no per-directory access controls; keep system-file backups and use host FAT
tools for deliberate system updates.

## Flush, interruption and checks

Successful copies flush the destination and close both handles. Renames flush
their metadata before returning; replacement frees the old destination's
clusters only after the new name is published and the source name is removed.
A same-directory rename to an unused name edits the source slot and needs no
free data clusters. Cross-directory moves may need a free slot or directory
cluster. These are [FAT16 write guarantees](fat16.md#rename-ordering), with
documented repair windows after power loss or failed I/O.

`sync` opens the root directory, flushes its filesystem and closes the handle,
including after a flush error. Success is silent; arguments are rejected.
On a read-only volume it succeeds without a disk write. FAT mutations already
flush as documented, so `sync` is an explicit flush request and does not add
journaling, atomic replacement or recovery from interrupted writes.

Run `.\rum.ps1 test -Suite file-tools -Jobs 4` on Windows, or
`./rum test --suite file-tools -j4` in Ubuntu/WSL. The focused group checks
short I/O and explicit move ordering on the host, injects failures at every FAT
rename write/flush stage, and exercises tools in ELF/16 MiB and ISO/64 MiB QEMU
boots. It compares copied bytes with host FAT tools, reboots, and checks busy,
full and read-only media. All disks are disposable. Logs and failure reports
are under `build/test-artifacts/file-tools/`.
