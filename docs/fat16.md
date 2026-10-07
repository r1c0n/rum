# FAT16 disks

rum mounts a supported FAT16 volume at `/disk` when an image is attached as the
secondary IDE master. Embedded files remain at the RAM root. Kernel callers can
read and write files, resize them, and create and remove directories. Mounting
and normal boot never format, repair, or write the image.

The common kernel filesystem API provides these operations and directory iteration.
The normal [userspace shell](shell.md) can list, read, create and replace disk
files, change directories, make and remove directories, and launch disk ELFs.
It uses the same [filesystem syscalls](filesystem-syscalls.md) as other user
programs. The kernel recovery shell retains its RAM-file commands.

## Create an image with host files

Install mtools on Ubuntu or WSL if needed:

```sh
sudo apt install mtools
```

From the repository, create a new disposable 16 MiB image, format it, and copy
a file into it:

```sh
make create-disk DISK_IMAGE=build/fat16.raw DISK_SIZE_MIB=16
mformat -i build/fat16.raw -T 32768 -c 2 -v RUM ::
printf 'Hello from the host!\n' > build/note.txt
mcopy -i build/fat16.raw build/note.txt ::/NOTE.TXT
mmd -i build/fat16.raw ::/DOCS
mdir -i build/fat16.raw ::/
make run DISK_IMAGE=build/fat16.raw
```

The creation command refuses an existing path. `mformat` is the separate,
explicit formatting operation; use it on the new disposable image. The image
contains FAT16 directly at sector zero, without a partition table. To boot it
through QEMU's direct ELF loader, use `make run-kernel` with the same
`DISK_IMAGE` value.

From PowerShell, use the Windows launcher for creation and boot and run mtools
inside WSL:

```powershell
.\rum.ps1 create-disk -DiskImage build\fat16.raw -DiskSizeMiB 16
wsl.exe -d Ubuntu --cd $PWD.Path --exec mformat -i build/fat16.raw -T 32768 -c 2 -v RUM ::
.\rum.ps1 run -DiskImage build\fat16.raw
```

The serial log reports `rum_fat16: ok` on success; the console shows
`FAT16 /disk (read/write)` for a writable device, or `FAT16 /disk (read only)`.
A blank, unsupported, or invalid volume reports a
filesystem error and boot continues with RAM files. No disk reports
`unavailable filesystem`.

Writes change the supplied image. Use a disposable copy for experiments.
`fat16_mount_read_only()` lets kernel callers mount without permitting writes.
This is independent of QEMU's `readonly=on` device option, which some IDE
configurations refuse before boot; see [Raw disks and block devices](block-devices.md#attach-an-existing-image).

For a direct ELF boot with a forced read-only FAT mount, add
`-append rum.disk-readonly` to QEMU. This is a kernel mount policy; QEMU still
attaches the underlying IDE image normally. It does not change host permissions.

## Supported format

| Property | Support |
| --- | --- |
| Placement | Unpartitioned volume starting at sector zero; hidden-sector count zero |
| Sector size | 512 bytes, matching the block device |
| Allocation tables | Exactly two FAT copies, at most 256 sectors each |
| Cluster size | Power of two from 1 to 64 sectors, at most 32 KiB |
| Root directory | Fixed nonzero entry count, a multiple of 16 entries |
| Subdirectory | At most 65,536 slots, including deleted and other skipped entries |
| Names | Common layer's uppercase ASCII 8.3 subset |
| Reads | Explicit offsets, partial-sector reads, fragmented chains, empty files, and EOF |
| Mutations | Create, replace, overwrite, append, truncate, delete, mkdir, and empty-directory removal |
| Writable-tree audit | At most 65,536 scanned slots across the tree; directory depth at most 16 |

FAT type comes from the derived data-cluster count rather than the boot-sector
text label. FAT16 has 4,085–65,524 data clusters. FAT12, FAT32, exFAT, partitioned
images, and larger metadata than the limits above are unsupported. Region and
cluster calculations follow Microsoft's [FAT format specification](https://www.pcjs.org/documents/papers/microsoft/MS_FAT_OVERVIEW_103-2000-12-06.pdf).

Volume-label, deleted, long-name, and `.`/`..` directory entries are skipped.
Short names outside rum's ASCII subset are also skipped, including OEM names
and aliases containing `~`. Compatible short entries attached to a long name
are accessible by their short spelling; the long name itself is never decoded.
File size, first-cluster fields, and directory attributes are validated before
an entry is exposed. The FAT16 high cluster word must be zero.

Directory iteration ends at a zero entry or the end of its bounded directory
storage. A full directory does not need an extra zero entry. `.` and `..` path
behavior belongs to the [common path layer](filesystems.md), which prevents
climbing above the mounted root.

## Validation and failures

Mounting checks the boot signature, BPB fields, total sector count, derived
FAT/root/data regions, and the FAT's capacity for all data clusters. The declared
volume must fit inside the block device; trailing device sectors are not part
of the volume. Both complete FAT copies are read and must agree, including
padding and reserved entries. The backend does not choose or repair a preferred
copy when they differ.

The FAT is cached in one heap allocation, at most 128 KiB, and a fixed bitmap
detects repeated clusters. Every file or subdirectory entry validates its
complete chain before lookup or iteration exposes it. Free, reserved, bad,
out-of-volume, and repeated clusters fail with `FS_IO_ERROR`. A file's chain
must contain exactly enough clusters for its recorded size; premature end,
extra tail clusters, and allocated empty files are rejected. These checks
apply even when the caller requests only the first byte.

Geometry and FAT-copy failures reject the mount and release its allocation.
Directory and file corruption is detected when that entry is encountered;
mounting does not recursively scan every directory or perform a whole-volume
cross-link audit. Before the first mutation, a separate bounded walk checks
every live short entry's cluster ownership, even when its name is outside rum's
supported subset. Shared chains, malformed chains, and incorrect `.`/`..`
entries reject the mutation before writes. The audit never frees unreachable
allocated clusters or repairs metadata. A tree over its audit budget returns
`FS_UNSUPPORTED`; excessive directory depth returns `FS_TOO_DEEP`.
Other RAM files remain usable. A directory larger than the slot limit returns
`FS_UNSUPPORTED`.

Block read errors retain ATA status and error-register diagnostics. If a later
sector read fails, `fs_read` reports the error and the bytes already copied;
the failing sector contributes no bytes. A failed sector is not cached, so
callers can retry after the device recovers. Mounting is also transactional
when FAT allocation, either FAT read, or namespace registration fails.

The backend caches allocation metadata and one data sector. Keep the attached
device and its contents unchanged until unmounted. Host edits while mounted,
automatic repair, journaling, and general crash recovery are unsupported.

## Writing files and directories

`fs_replace` creates a file or replaces its complete contents, including an empty
file. Through a `FS_WRITE` reference, `fs_write` overwrites or appends and
`fs_truncate` changes the size. Growth through truncation fills with zeroes.
A byte write cannot start beyond EOF. Sizes must fit the FAT16 32-bit size field
and the volume's data capacity; arithmetic overflow returns `FS_RANGE`.

For example, an explicit kernel action can create or replace a note:

```c
static const char note[] = "rum on disk\n";
enum fs_error error = fs_replace(NULL, "/disk/NOTE.TXT", note, sizeof note - 1);
/* Check error before relying on the new contents. */
```

Files use copy-on-write, including small overwrites and nonzero truncation.
The entire resulting file needs a new chain while the old chain remains allocated.
A full volume can therefore return `FS_NO_SPACE` when overwriting or shrinking
an existing file. Truncation to zero and deletion do not need a new data chain.
Each mutation uses a temporary FAT snapshot, at most 128 KiB; allocation failure
returns `FS_NO_MEMORY` before changing the disk.

`fs_mkdir` initializes a zeroed cluster with `.` pointing to itself and `..`
pointing to its parent's first cluster, or zero for the fixed root directory.
Subdirectories grow by one zeroed cluster when their slots fill. The fixed root
cannot grow. Deleted slots and freed clusters are reused. Removing a directory
with any live entry besides `.`/`..` returns `FS_NOT_EMPTY`, including names that
rum cannot list. Open files and working directories prevent removal with `FS_BUSY`.

FAT's read-only attribute rejects file mutations with `FS_ACCESS`. Associated
long-name entries are kept when a compatible short entry's contents change.
Deleting such an entry returns `FS_UNSUPPORTED`; rum does not edit LFN sequences.
An orphan LFN prefix blocks reusing its following slot. New names use uppercase
8.3 spelling. New timestamps use 1980-01-01 because rum has no RTC clock.

## Write ordering and interruption

Every successful mutation flushes synchronously. File creation and replacement
use this order:

1. Write every sector of the new chain, including zeroed padding, and any new
   directory storage. Flush the data.
2. Write changed sectors in the first FAT copy and flush, then do the same for
   the second copy. Publish the cached FAT only after both flushes succeed.
3. Write the directory entry with the new first cluster and size, then flush.
4. Free the old chain in both FAT copies, flushing each copy again.

Directory creation follows the same data/FAT/entry order. Directory growth links
only zeroed storage. Deletion first writes and flushes the deleted-entry marker,
then frees its old chain in both copies. A consumed end marker keeps a zero
marker in the next slot so stale bytes cannot reappear as live files. Sector
writes check volume offsets and preserve neighboring entries. `fs_flush` issues
an additional device flush; unmount itself performs no I/O.

This ordering assumes the device honors successful flushes. Newly reachable
file data has been written and both FAT copies flushed before publication.
Existing data is kept until the new directory entry is flushed. It is
**not a crash-atomic transaction**: a power loss or torn write can still leave
unequal FAT copies, unreachable allocated clusters, an incomplete directory
entry, cross-links from torn cluster fields, or a deleted/replaced file whose old
chain has not been freed. Directory growth can leave an empty linked cluster.
FAT clean/error flags are not maintained as recovery markers. Host repair may be
needed; rum neither chooses a preferred
FAT copy nor repairs a damaged volume.

Once an attempted write might have changed the disk, a later I/O or flush error
faults the mount. `fat16_info().faulted` becomes true, `writable` becomes false,
and further backend lookup, reads, writes, and flushes return `FS_IO_ERROR` until
unmount. Existing references can close and RAM files keep working. This prevents
allocation from uncertain metadata; it does not roll back disk bytes. Close
references, stop QEMU, preserve the image, and inspect or repair a copy with
host FAT tools before mounting again. A fresh mount still checks both FATs.
Zero-byte reads and writes still validate their references without invoking a
backend, as specified by the common API.

For `fs_write`, an error before the directory entry is successfully flushed
reports zero transferred bytes, even if staging sectors changed. If publication
succeeded but freeing the old chain fails, it reports the full requested byte
count together with the error. Inspect both fields. `fs_replace`, `fs_truncate`,
and namespace operations return an error without a byte count; an error after
publication can mean that the new state reached disk. Failure before any
attempted write leaves the mount usable and its allocations unchanged.

## Kernel use and lifetime

Boot calls `fat16_mount(ata_device())` after RAM files and tasks initialize.
Kernel code can mount a different supported block device with the same call
after `fs_initialize`; only one disk backend can occupy `/disk`. Write access
requires a device not marked read-only and both write and flush callbacks.
Otherwise the backend mounts read-only. `fat16_mount_read_only` forces that
policy even for a writable device.

```c
fs_reference file;
enum fs_error error = fs_open(NULL, "/disk/NOTE.TXT", FS_READ, &file);
if (error == FS_OK) {
    unsigned char bytes[128];
    struct fs_io_result result = fs_read(file, 0, bytes, sizeof bytes);
    /* Use result.transferred and result.error; bytes need not be text. */
    fs_close(file);
}
```

`fat16_info()` returns the mounted geometry without I/O, or a zeroed record
when unavailable. Directory-slot positions identify objects within a mount;
the common layer adds a mount generation. Retain and release use fixed tables
without allocation or disk I/O, including when reaping process directories.
Byte writes and resizing preserve the slot identity, so other open references
see the updated contents and size. Whole-file replacement and removal require
all references to the object to close first.

Close disk references and leave disk working directories before calling
`fat16_unmount()`. An open owner returns `FS_BUSY` and keeps the FAT allocated.
Successful unmounting releases it. The generic `fs_unmount_disk()` also invokes
the backend cleanup callback, so both entry points release the cache. A
subsequent mount acquires a fresh FAT snapshot and generation.

## Check a change

```sh
make test-fat16-host  # mtools images, portable backend, read faults and rollback
make test-fat16       # Host checks plus real ATA reads in 16/64 MiB QEMU guests
make test-fat16-write-host # Writable backend, full media, every write/flush failure
make test-fat16-write # Real ATA writes, interruptions, and host FAT validation
make test            # Complete regression suite
```

The tests generate disposable images under `build/tests/fat16/`. They compare
host FAT reads and listings with guest physical-memory dumps, test fragmented
files and multi-cluster directories, and check the whole image hash after each
guest to prove no writes occurred. Invalid geometry, FAT disagreement, invalid
entries, cycles, truncated chains, and out-of-range clusters have separate
fixtures. Normal ISO and ELF boots also exercise valid and rejected volumes.
Logs and guest dumps are saved under `build/test-artifacts/fat16/`.

Write tests compare disk bytes with `mcopy`, list directories with `mdir`, and
independently audit both FAT copies, cluster ownership, sizes, dot entries, and
unreachable allocations after QEMU exits. They cover boundary sizes, zero-filled
growth, replacement, truncation, deletion/recreation, slot and cluster reuse,
directory growth, full media, read-only devices, heap failure, and interruptions.
Fresh QEMU boots also read the saved files through a forced read-only mount.
Canary sectors beyond the BPB volume and an unrelated file's entries and clusters
must stay identical. Each host write/flush stage is tested with no transfer,
partial-sector transfer, and a complete transfer followed by an error. Faulted
images may need repair; the failure report records those cases separately from
clean volumes. Images, serial logs and `faults.json` are saved under
`build/test-artifacts/fat16-write/`. Tests never use a caller's disk image.
