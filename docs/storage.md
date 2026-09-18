# Heap and RAM files (milestone 6)

rum remains at unreleased `0.1.0`. After physical allocation and paging start,
the kernel initializes a heap, creates a flat RAM filesystem, and copies embedded
boot files into it. No disk image, GRUB module, or host filesystem is needed at runtime.

## Try it

Start `./rum.ps1 run` and type:

```text
ls
cat welcome.txt
write notes.txt hello from rum
cat notes.txt
mem
rm notes.txt
```

`write` creates or replaces a file; without text it creates an empty file.
Leading spaces before its text are skipped and internal/trailing spaces are
preserved. There is no quoting or redirection. `cat` adds a newline if needed
and displays binary/control bytes as dots, except newline and tab. The C API
preserves every byte, including zero bytes. `ls` reports filenames and byte sizes.
`mem` reports mapped heap bytes, aligned live payload bytes, allocations, file
count, and file bytes. Heap headers are excluded from payload counts.

One root directory holds at most 64 files, each at most 64 KiB, within the heap
and available RAM budget. Names contain
1–63 ASCII letters/digits or `._-`; `.` and `..` are rejected. An optional leading
`/` names the same root file. Subdirectories and paths with additional slashes
are unsupported. The shell's existing 255-character command-line limit also
bounds text entered with `write`; kernel callers can store larger/binary files.
All edits disappear on reboot, when embedded files are restored.

## Heap

`kernel/heap.c` uses first-fit allocation and a doubly linked list of in-heap
headers. Payload pointers are aligned to 16 bytes. Blocks split when there is
enough room for another header and an aligned payload. Freeing merges adjacent
free blocks; freed space can be reused without taking more physical pages.

The heap owns virtual addresses `0x40000000` through `0x403fffff` (4 MiB).
It initially maps one page, then allocates physical frames and writable,
supervisor-only mappings on demand. Other mapping callers must avoid this
window. Failure during growth removes every newly created mapping and frees its
frame, leaving previous mappings and blocks intact. The paging layer reclaims
an empty page table if initialization fails. Pages are retained after `kfree`
for later reuse; this milestone does not shrink the heap or return idle heap
pages to the physical allocator. The next page after the heap remains unmapped
unless another kernel caller maps it.

The API in `include/rum/heap.h` provides:

| Function | Contract |
| --- | --- |
| `kmalloc(size)` | Uninitialized allocation; zero size, overflow or exhaustion returns `NULL` |
| `kcalloc(count, size)` | Check multiplication overflow, allocate and zero the requested bytes |
| `krealloc(pointer, size)` | Preserve existing bytes; shrink/extend in place when possible; otherwise move |
| `kfree(pointer)` | Merge free neighbors; `NULL` succeeds, invalid/interior/double frees return false |
| `heap_stats()` | Mapped bytes, live aligned payload capacity, reusable bytes and live allocation count |

`krealloc(NULL, size)` behaves like allocation. `krealloc(pointer, 0)` frees and
returns `NULL`. A failed nonzero resize leaves a valid original allocation
unchanged. Invalid pointers are compared against live payload addresses without
reading memory before them. As with other allocators, a stale pointer cannot
identify a former allocation after its address has been reused.

This is a single-CPU kernel heap. Operations save/disable/restore interrupt flags
while changing headers/mappings. Callers must run in foreground code; IRQ handlers
only queue work and must never allocate. There are no SMP locks, user heaps,
garbage collection, allocation poisoning, or memory compaction.

## RAM filesystem

`kernel/ramfs.c` keeps heap-allocated file nodes and exact-size copied payloads.
The API is foreground-only and owns all stored bytes:

```c
ramfs_put("notes.txt", "hello", 5);
const unsigned char *data;
size_t size;
if (ramfs_read("notes.txt", &data, &size)) {
    /* data[0..size) is borrowed; it is not necessarily zero-terminated. */
}
ramfs_remove("notes.txt");
```

`ramfs_put` copies first and commits only after all allocations succeed. A failed
create/replace leaves the filesystem and any original file unchanged. Replacing
a file from its own borrowed bytes is supported. Borrowed pointers remain valid
until that file is replaced or removed. Removing a file frees both its node and
payload. Listing callbacks can return false to stop, and must not mutate the
filesystem during iteration. Empty files have a zero length and may have a null
data pointer. The implementation has no file descriptors, permissions, mount
table, VFS, disk driver, or persistence.

## Embedded files

Edit or add files in `assets/ramfs/`, then run `./rum.ps1 build` or `make`.
`scripts/embed-files.py` validates filenames/limits, sorts files, and generates
binary-safe C byte arrays in `build/embedded-files.c`. They are linked into
read-only kernel data, then copied into mutable RAM files at boot. The default
files are `readme.txt` and `welcome.txt`. There is no terminating-zero requirement.

Every build checks the asset directory, including removed files. If generated
contents are unchanged, its timestamp is preserved and the generated object
stays current. If assets change, the kernel and ISO rebuild. A failed install
removes files created by that attempt; it will not overwrite files already
present. The generator and runtime use the same 64-file, 64-KiB and name limits.
Generated C/objects are ignored build artifacts; commit the assets and generator.
Git preserves asset bytes without line-ending conversion. The read-only manifest
is available through `include/rum/embedded.h` for kernel code that needs to inspect
the embedded filenames, lengths or original bytes.

## Checks

`make test` / `./rum.ps1 test` run production heap/filesystem code in host tests
with a simulated page boundary, then in isolated QEMU kernels at 16 and 64 MiB.
Checks cover alignment, zero/overflow, split/coalesce, fragmentation with payload
canaries, calloc, realloc preservation, invalid frees, virtual exhaustion/reuse,
partial physical growth failures, and initialization page-table failure rollback.
File checks cover binary/empty/max-size files, paths, capacity, listing and
early termination, unlinking, failed atomic writes, and embedded installation.

Normal boots independently inspect heap page-table permissions, physical frame
ownership, block headers/accounting, file nodes, and every embedded byte against
the source assets. Keyboard-driven tests use list/read/write/replace/remove,
usage errors, memory reports, and VGA/serial output while the timer continues.
The generator is checked for binary/empty content, stable output, asset removal,
and invalid names/sizes. Existing exception, IRQ, paging and RAM-size tests remain.
Logs, reports, dumps and screenshots are in `build/test-artifacts/`.
