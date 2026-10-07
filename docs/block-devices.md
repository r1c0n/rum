# Raw disks and block devices

rum can attach one raw disk as the secondary IDE master in QEMU. The kernel
identifies it at boot and provides sector reads, writes and cache flushing to
kernel callers. A supported [FAT16 volume](fat16.md) mounts at `/disk`, writable
when the device supports writes and flushing.
The userspace shell accesses disk files under `/disk`; `write /disk/NOTE.TXT`
persists on the attached image. Root RAM files remain temporary.

Normal boot identifies the disk and attempts a FAT16 mount. It does
not format, resize, repair or write the image. Without a supported disk, rum
continues to its usual shell.
The serial `rum_disk:` line reports discovery, ATA status/error bytes in decimal,
and the number of accessible sectors.

## Create a disposable image

From PowerShell:

```powershell
.\rum.ps1 create-disk -DiskImage build\scratch.raw -DiskSizeMiB 16
.\rum.ps1 run -DiskImage build\scratch.raw
```

From Ubuntu or WSL:

```sh
make create-disk DISK_IMAGE=build/scratch.raw DISK_SIZE_MIB=16
make run DISK_IMAGE=build/scratch.raw
```

The separate creation command makes a blank, zero-filled raw file. It refuses
every existing path, including symlinks, and requires the parent directory to
exist. It accepts 1–131071 MiB; the default is 16 MiB. It does not install a
partition table or filesystem. The portable equivalent is:

```sh
python3 scripts/disk-image.py build/scratch.raw --size-mib 16
```

Keep test images under `build/` or outside the repository. Release ZIPs contain
the ISO and documentation, without mutable disk images.

## Attach an existing image

`-DiskImage` and `DISK_IMAGE` also work with `run-kernel` and `debug`. Paths with
spaces or commas are supported; quote them in your shell. Omitting the option
attaches no disk. A missing path, directory, empty image, unaligned size or image
larger than 128 GiB is rejected. Existing images are never created or resized
by the launchers.
Generated boot images are rejected as disk paths before rebuilding them,
including hard links detected by the shared validator.

The ISO's CD-ROM occupies primary master (`ide.0`, unit 0). The raw disk uses
secondary master (`ide.1`, unit 0). The launchers suppress QEMU's implicit empty
CD-ROM so it cannot occupy the disk's slot. VGA, PS/2 input, PIT, PIC and serial
remain available through both boot paths.

QEMU opens the disk as raw with write-back caching and `werror=report` /
`rerror=report`. I/O failures reach the ATA driver instead of stopping the VM.
Guest writes are real writes to the supplied image; flush before relying on
their persistence. Use a disposable copy for experiments.

To request a read-only backend:

```powershell
.\rum.ps1 run -DiskImage build\scratch.raw -DiskReadOnly
```

```sh
make run DISK_IMAGE=build/scratch.raw DISK_READ_ONLY=1
```

The tested QEMU IDE implementation refuses this backend with `Block node is
read-only` before rum boots. The image remains intact. Boot without the disk
to use rum in that case. ATA IDENTIFY does not advertise host file permissions;
a permission failure during a write is reported as an ATA I/O error rather
than a guessed read-only capability.

## Kernel API

Include `rum/block.h` for the generic interface and `rum/ata.h` to obtain the
discovered device. These are kernel interfaces; userspace cannot access ports
or issue raw sector requests.

```c
uint8_t sector[512];
struct block_device *disk = ata_device();
struct block_result result = block_read(disk, 0, 1, sector, sizeof(sector));
if (result.error != BLOCK_OK) {
    /* Inspect result.completed, result.status and result.device_error. */
}
```

| Operation | Contract |
| --- | --- |
| `block_read(device, lba, count, buffer, bytes)` | Read complete sectors into a kernel buffer |
| `block_write(device, lba, count, buffer, bytes)` | Write complete sectors from a kernel buffer |
| `block_flush(device)` | Complete the driver's cache-flush operation |
| `block_error_name(error)` | Describe a result for diagnostics |

`sector_size` and `sector_count` describe an initialized device. Counts and LBAs
are 64-bit in the generic API. Range checks use subtraction, and byte-size checks
use division, so arithmetic cannot wrap before a driver call. A zero-sector
request is a no-op, permits a null buffer and accepts an LBA at the end of an
online device. An LBA beyond the end is invalid even with count zero. Nonzero
requests need a buffer with enough bytes and a representable byte count.

The result distinguishes invalid arguments, sector ranges, absent devices,
read-only devices, busy requests, timeouts, device faults, I/O errors and
unsupported operations/devices. `completed` counts sectors that finished
successfully. A failed write may also have changed its in-flight sector; there
may also be bytes from an incomplete read beyond the completed prefix. There
is no atomic multi-sector write or rollback guarantee. Successful writes remain
subject to write caching until `block_flush` succeeds. A failed flush gives no
durability guarantee. Canaries outside the requested range must remain intact.

Use the API from boot or foreground task context on rum's single CPU. A short
interrupt-protected update acquires/releases the per-device lock. Polling keeps
the caller's interrupt state; it never enables interrupts during early boot.
A reentrant request returns `BLOCK_BUSY` instead of waiting. Do not call from
an interrupt handler or bypass the wrappers through operation pointers.

## ATA limits and failures

The driver uses 16-bit PIO on ports `0x170`–`0x177` and alternate status/control
at `0x376`. IDE interrupts stay disabled. It accepts ATA devices with LBA28 and
512-byte logical sectors; ATAPI and other logical sector sizes are unsupported.
It uses the reported LBA28 capacity, capped by the command format's 128 GiB
address space. Some QEMU versions report one fewer sector at that upper limit.
LBA48, DMA, secondary slaves and additional controllers are not implemented.

IDENTIFY also checks cache-flush support. An enabled write cache without a
supported flush command is rejected. A device without caching or flush support
can complete flush as a no-op. Transfers issue one sector per command, including
large requests, avoiding ATA's special count-zero meaning of 256 sectors.

Status-poll phases stop after at most 1,000,000 reads; flush completion allows
50,000,000 because it may wait for device media or a host `fsync`. The bound works
before PIT interrupts start; it is an iteration limit, not a calibrated duration.
Busy status is checked before other status bits. Device faults and ATA errors
retain their status and error-register bytes in the result. Timeouts, device
faults and device disappearance take the device offline. Later calls fail before
issuing another command, since a stalled write may still be outstanding. Reboot
to probe again. An ordinary command error allows another request, and stale ERR
is cleared by issuing the next command.

## Verify a storage change

```sh
make test-block
make test
```

`test-block` runs API tests, a scripted port model using the production ATA
driver, image-tool tests and QEMU tests at 16 and 64 MiB. The host creates known
sector patterns, compares exact guest memory dumps, writes first/last sectors
and a 257-sector range, checks every untouched sector, and rereads the result
after a separate boot. Invalid ranges, zero counts and overflow requests must
leave the entire image unchanged.

QEMU's `blkdebug` backend injects read, write, permission and flush errors. The
port model covers stuck BSY, missing DRQ, initialization timeouts, floating/missing
hardware, device faults, unsupported IDENTIFY capabilities, partial transfers,
reentrant requests and interrupt-state preservation. True polling timeouts are
modelled rather than inferred from a host I/O error.

Both production boot paths are checked with and without a disk, including a
keyboard/shell command and confirmation that boot leaves all image bytes intact.
Logs and guest dumps are saved under `build/test-artifacts/block/`; images are
created in temporary directories and discarded. No test writes to a user-supplied
disk. The QEMU harness also runs on Windows with `--qemu <executable>`.

On Windows, `tests/disk-launcher-test.ps1` checks wrapper argument handling with
recorded calls; `rum.ps1 test` runs it after the WSL suite.

QEMU option and fault-injection contracts are documented in its
[system manual](https://www.qemu.org/docs/master/system/qemu-manpage.html) and
[block API schema](https://gitlab.com/qemu-project/qemu/-/blob/v8.2.2/qapi/block-core.json).
