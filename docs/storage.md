# Heap and RAM files

rum's normal filesystem lives entirely in memory. Files embedded in the kernel
are copied into a writable RAM filesystem during boot. Files created or changed
at runtime disappear when QEMU restarts.

## Using files from the shell

```text
> ls
> cat welcome.txt
> write notes.txt hello from rum
> cat notes.txt
> mem
> rm notes.txt
```

`write` creates or replaces a file. Omitting the text creates an empty file.
Internal and trailing spaces are preserved. `cat` displays newline and tab but
replaces other control or binary bytes with dots; the underlying file still
contains its original bytes.

The filesystem has one flat root:

- Up to 64 files.
- Up to 64 KiB per file.
- Names 1–63 characters long.
- ASCII letters, digits, `.`, `_`, and `-` only.
- An optional leading `/`; `/notes.txt` and `notes.txt` name the same file.
- No directories, permissions, handles, or persistent disk backend.

`.` and `..` are rejected as filenames. The shell's 255-character input limit
usually bounds text created interactively before the 64 KiB file limit matters.

## Understanding `mem`

The `mem` command reports:

- Mapped heap capacity.
- Live aligned payload capacity.
- Number of live heap allocations.
- RAM-file count and payload bytes.

Freeing an object makes its block reusable but does not unmap heap pages.
Therefore mapped bytes can stay high while live bytes fall. This is expected and
is different from a leaked live allocation.

## Embedding files in the ISO

Put source files in `assets/ramfs/` and rebuild:

```powershell
./rum.ps1 build
```

or:

```sh
make
```

`scripts/embed-files.py` validates names and limits, sorts entries, and produces
binary-safe C arrays under `build/`. The arrays are linked into read-only kernel
data and copied into mutable RAM files during boot.

Commit files in `assets/ramfs/`. Do not commit `build/embedded-files.c` or other
generated build output. Removing an asset and rebuilding removes it from the
next ISO.

## Heap API

The heap occupies `0x40000000`–`0x40400000`. It begins with one mapped page and
grows with cleared writable supervisor mappings. Blocks are 16-byte aligned,
split when useful, and coalesced with adjacent free blocks.

| API | Behavior |
| --- | --- |
| `kmalloc(size)` | Allocate uninitialized bytes; returns `NULL` for zero, overflow, or exhaustion |
| `kcalloc(count, size)` | Check multiplication, allocate, and zero the requested bytes |
| `krealloc(pointer, size)` | Resize while preserving data; may move the object |
| `kfree(pointer)` | Release a live allocation and merge neighbors |
| `heap_stats()` | Report mapped, live, reusable, and allocation totals |

`krealloc(NULL, size)` behaves like allocation. `krealloc(pointer, 0)` frees and
returns `NULL`. A failed nonzero resize preserves the original allocation.
`kfree(NULL)` succeeds; interior, unknown, and double-free pointers fail.

Heap operations are for foreground code on one CPU. Do not call them from an
IRQ handler. Growth is transactional: if mapping a later page fails, newly
created mappings and frames are removed while existing allocations remain valid.

## RAM-filesystem API

```c
if (!ramfs_put("notes.txt", "hello", 5)) {
    /* Invalid name, size limit, file limit, or allocation failure. */
}

const unsigned char *bytes;
size_t size;
if (ramfs_read("notes.txt", &bytes, &size)) {
    /* bytes[0..size) is borrowed and may contain binary data. */
}

ramfs_remove("notes.txt");
```

`ramfs_put` copies the new payload before committing the change. If allocation
fails, an existing file with the same name remains untouched. This also makes
replacement safe when the input pointer refers to that file's currently
borrowed bytes.

`ramfs_read` returns a borrowed pointer valid until that file is replaced or
removed. The payload is not NUL-terminated unless the stored data includes a
NUL. Empty files have length zero and may return a null byte pointer.

`ramfs_list` visits files in filesystem order. A visitor may stop by returning
false but must not mutate the filesystem during iteration.

## Common problems

- **A file vanished after reboot:** runtime edits are intentionally temporary;
  add the file to `assets/ramfs/` if it should appear on every boot.
- **`write` fails:** check the filename, 64-file limit, 64 KiB limit, and heap
  usage with `mem`.
- **`cat` shows dots:** the file contains control or binary bytes; the shell is
  sanitizing display output, not changing the stored file.
- **Heap mapped bytes do not shrink:** pages stay mapped for reuse by design.
- **A borrowed file pointer became invalid:** a replacement or removal ended its
  lifetime; copy the data if it must survive mutation.

Run `make test-host` after changing allocation, block splitting/coalescing,
RAM-file mutation, or embedded asset generation. Use `make test` for paging
growth and low-memory behavior in QEMU.
