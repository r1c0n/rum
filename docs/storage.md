# Heap and RAM files

After physical allocation and paging are initialized, rum creates a kernel
heap and a flat RAM filesystem. Embedded files are copied into it at boot.
Runtime file access uses RAM only.

## Working with files

```text
ls
cat welcome.txt
write notes.txt hello from rum
cat notes.txt
mem
rm notes.txt
```

`write` creates or replaces a file. Without text it creates an empty file.
Leading spaces before text are skipped; internal and trailing spaces are
preserved. `cat` displays binary/control bytes as dots, except for newline and
tab, and adds a final newline if needed. The C API preserves all bytes.

`ls` reports names and sizes. `mem` reports mapped heap bytes, live aligned
payload capacity, allocation count, file count, and stored file bytes.
Heap headers are excluded from payload counts.

The filesystem holds up to 64 files of up to 64 KiB each, subject to heap space.
Names contain 1–63 ASCII letters, digits, dots, underscores, or hyphens.
`.` and `..` are rejected. An optional leading `/` refers to the same root
file; subdirectories are unsupported. The shell's 255-character line limit
bounds text entered with `write`; kernel callers can store larger binary data.
Rebooting restores the embedded files and discards edits.

## Heap

`kernel/heap.c` uses first-fit allocation with a doubly linked list of block
headers. Payloads are aligned to 16 bytes. Blocks split when there is room
for another header and an aligned payload. Freeing merges adjacent free
blocks for reuse.

The heap reserves virtual addresses `0x40000000`–`0x403fffff` (4 MiB).
It starts with one mapped page and grows by allocating frames and writable
supervisor mappings. Failed growth removes new mappings and frees their frames,
leaving existing allocations intact. An empty page table is reclaimed if
initialization fails. Freed heap pages stay mapped for reuse.

The interface is declared in `include/rum/heap.h`:

| API | Behavior |
| --- | --- |
| `kmalloc(size)` | Allocate uninitialized bytes; return `NULL` for zero size, overflow, or exhaustion |
| `kcalloc(count, size)` | Check multiplication overflow, allocate, and zero the requested bytes |
| `krealloc(pointer, size)` | Resize while preserving data; move the allocation if necessary |
| `kfree(pointer)` | Free and merge neighbors; reject invalid, interior, or double-free pointers |
| `heap_stats()` | Report mapped bytes, live payload capacity, reusable bytes, and allocation count |

`krealloc(NULL, size)` allocates. `krealloc(pointer, 0)` frees and returns
`NULL`. A failed nonzero resize preserves the original allocation.
`kfree(NULL)` succeeds. Pointer validation compares live payload addresses
without reading before an arbitrary pointer; it cannot distinguish a stale
pointer after the same address has been reused.

Heap operations preserve interrupt flags while updating headers and mappings.
The heap supports one CPU and must be called from foreground code, never from
IRQ handlers.

## Filesystem API

`kernel/ramfs.c` stores heap-allocated nodes and copied payloads. Calls run in
foreground code. For example:

```c
ramfs_put("notes.txt", "hello", 5);
const unsigned char *data;
size_t size;
if (ramfs_read("notes.txt", &data, &size)) {
    /* Use data[0..size); it may contain binary bytes. */
}
ramfs_remove("notes.txt");
```

`ramfs_put` copies data before committing a change. Failed creation or
replacement leaves existing files unchanged, including when replacing a file
from its own borrowed bytes.

`ramfs_read` returns borrowed bytes valid until the file is replaced or removed.
Data is not necessarily zero-terminated. Empty files have zero length and may
return a null data pointer. Removal frees the node and payload. Listing
callbacks can return false to stop; they must not modify files during iteration.

The filesystem has one root directory and no file descriptors, permissions,
mounts, or disk persistence.

## Embedded files

Add or edit files in `assets/ramfs/`, then run `.\rum.ps1 build` or `make`.
`scripts/embed-files.py` validates names and limits, sorts files, and generates
binary-safe arrays in `build/embedded-files.c`. They are linked into read-only
kernel data and copied into mutable files at boot. The default files are
`readme.txt` and `welcome.txt`.

Every build checks the asset directory, including deletions. Unchanged generated
content keeps its timestamp; changed assets rebuild the kernel and ISO.
Git preserves asset bytes without line-ending conversion.

Installation rolls back files created by a failed attempt and does not overwrite
existing files. The generator and runtime share the same capacity and filename
limits. `include/rum/embedded.h` exposes the read-only filename, length, and
byte manifest. Commit source assets; generated arrays and objects belong in
the ignored build directory.

## Tests

Host tests run the heap and filesystem with a simulated page backend.
Isolated QEMU kernels exercise them at 16 and 64 MiB. Cases cover alignment,
splitting and coalescing, fragmentation, resizing, overflow, invalid frees,
exhaustion, and rollback after partial growth failures.

File tests cover binary and empty data, size/name limits, listing, removal,
atomic replacement, and embedded installation. Normal boots inspect heap
mappings and headers, file nodes, and embedded bytes, then use the shell to
create, read, replace, and remove files. Generator tests check stable output,
asset removal, and invalid inputs. Artifacts are in `build/test-artifacts/`.
