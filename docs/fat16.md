# Read-only FAT16 disks

rum mounts a supported FAT16 volume at `/disk` when an image is attached as the
secondary IDE master. Embedded files remain at the RAM root. Disk files are
read-only: mounting, reading, and unmounting never format, repair, or write the
image.

The common kernel filesystem API can read the volume and iterate directories.
The current kernel shell's `ls`, `cat`, `write`, `rm`, and `run` commands still
use RAM files. Filesystem syscalls and shell access to `/disk` are planned
separately.

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
`FAT16 /disk (read only)`. A blank, unsupported, or invalid volume reports a
filesystem error and boot continues with RAM files. No disk reports
`unavailable filesystem`.

The filesystem is read-only even when QEMU opens the raw device with write
permission. This is independent of QEMU's `readonly=on` device option, which
some IDE configurations refuse before boot; see [Raw disks and block
devices](block-devices.md#attach-an-existing-image).

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
| Mutations | Unsupported; the mount rejects writes with `FS_READ_ONLY` |

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
cross-link audit. An operation on corrupt metadata fails without writing to
the volume. Other RAM files remain usable. A directory larger than the slot
limit returns `FS_UNSUPPORTED`.

Block read errors retain ATA status and error-register diagnostics. If a later
sector read fails, `fs_read` reports the error and the bytes already copied;
the failing sector contributes no bytes. A failed sector is not cached, so
callers can retry after the device recovers. Mounting is also transactional
when FAT allocation, either FAT read, or namespace registration fails.

The backend caches allocation metadata and one data sector. Keep the attached
device and its contents unchanged until unmounted. Host edits while mounted,
automatic repair, writes, journaling, and crash recovery are outside this
read-only implementation.

## Kernel use and lifetime

Boot calls `fat16_mount(ata_device())` after RAM files and tasks initialize.
Kernel code can mount a different supported block device with the same call
after `fs_initialize`; only one disk backend can occupy `/disk`.

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

Close disk references and leave disk working directories before calling
`fat16_unmount()`. An open owner returns `FS_BUSY` and keeps the FAT allocated.
Successful unmounting releases it. The generic `fs_unmount_disk()` also invokes
the backend cleanup callback, so both entry points release the cache. A
subsequent mount acquires a fresh FAT snapshot and generation.

## Check a change

```sh
make test-fat16-host  # mtools images, portable backend, read faults and rollback
make test-fat16       # Host checks plus real ATA reads in 16/64 MiB QEMU guests
make test            # Complete regression suite
```

The tests generate disposable images under `build/tests/fat16/`. They compare
host FAT reads and listings with guest physical-memory dumps, test fragmented
files and multi-cluster directories, and check the whole image hash after each
guest to prove no writes occurred. Invalid geometry, FAT disagreement, invalid
entries, cycles, truncated chains, and out-of-range clusters have separate
fixtures. Normal ISO and ELF boots also exercise valid and rejected volumes.
Logs and guest dumps are saved under `build/test-artifacts/fat16/`.
