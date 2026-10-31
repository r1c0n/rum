# Building a release

The release archive contains `rum.iso`, the root README and `docs/`. The ISO
already includes the kernel, `/rum` system image and embedded RAM files. A user
disk is optional and is never included in the archive.

## Check the source and boot image

From a committed checkout in Ubuntu or WSL, run:

```sh
python3 scripts/release-check.py
```

The command copies tracked source into a fresh temporary directory, builds it,
runs the complete host and QEMU suite, validates the ZIP and boots the ISO
extracted from that ZIP. It uses the repository's cross-compiler by default;
pass `--cross-prefix /path/to/i686-elf-` or `--qemu /path/to/qemu-system-i386`
for another installation. `--jobs` controls build parallelism. Tests run
with four bounded workers after shared builds, fixtures and packaging finish.
Per-suite logs and timings are included in `test-artifacts/suite/`; see
[testing](testing.md).

Results are saved in `build/release-check/<commit>-<time>/`: `result.json`,
build/test logs, guest artifacts, `rum.elf`, `rum-system.img`, `rum.iso` and
`rum.zip`. A failed run records `passed: false` and exits unsuccessfully; its
files are diagnostic artifacts, not a validated release. The source checkout,
existing `build/` outputs and user disks are left in place.

For a focused storage/session check during development:

```sh
make test-release-integration
```

It boots the ISO and direct ELF with 16 and 64 MiB, writes `/disk/notes.txt`,
quits QEMU and verifies identical bytes in a fresh guest and through `mcopy`.
It also exercises no disk, rejected geometry, a forced read-only mount, media
that fills during use, deletion/cluster reuse and disk read/write/flush errors.
RAM commands, child launch, diagnostics, recovery and Snake must remain usable.
The archive's exact ISO boots without an attached disk at 16, 64, 256 and
1152 MiB. Serial and QEMU logs are under
`build/test-artifacts/release-integration/`.

Error injection uses QEMU's
[blkdebug driver](https://www.qemu.org/docs/master/devel/testing/blkdebug.html)
below the production ATA driver. Separate backend tests interrupt each FAT
write/flush stage, including partial-sector writes. All tests create their own
temporary disk images. They check exact file bytes, both FAT copies, cluster
ownership and canary sectors beyond the declared volume.

Process tests repeatedly fault or cancel a ring-3 program after real disk I/O
while it retains a disk handle and working directory. After reaping, file
references, objects, live heap allocations and private pages must return to
their baseline. CPU isolation, invalid user buffers and syscall packets have
separate regression cases.

For the concurrent workload specifically, run:

```sh
make test-integration-stress
```

This uses disposable FAT16 images and a test-only kernel/program pair. Each of
two representative boot/RAM configurations runs 16 rounds and 80 processes: two CPU workers,
file writers/readers, a sleeper and a foreground reader. It checks independent
physical mappings at equal virtual addresses, active CR3/TSS agreement and
private canaries while scheduling continues. Parent exit, fault and Ctrl+C leave
children holding real disk handles for the normal adoption/cleanup paths.
After every round, process, session, wait, file-reference, live-heap and physical
page counts must return to baseline, accounting for retained heap mappings.

After QEMU stops, Mtools must recover exact binary bytes, both FAT copies must
agree, unrelated files and sectors beyond the volume must be unchanged, and
`fsck.fat -n` must report a clean filesystem. Install `dosfstools` if that checker
is missing. Logs, filesystem checks and process/switch totals are saved in
`build/test-artifacts/integration-stress/`. The complete suite includes this test
alongside the dedicated no-yield register-preservation, lifecycle, console,
public-ABI and disk-failure tests.

## Verify packaging on each host

Python packaging needs no additional modules. After building the ISO:

```sh
python3 tests/package-test.py
```

On Windows, use:

```powershell
.\rum.ps1 build
py -3 tests/package-test.py
& .\tests\disk-launcher-test.ps1
```

The package test builds `rum.zip`, verifies every member against its source,
checks the ZIP's integrity and rejects extra boot payloads, debug outputs and
disk images. The launcher test records arguments without opening QEMU or
modifying a user disk. Extract the resulting archive and boot `rum.iso` to
check Windows QEMU as well:

```powershell
qemu-system-i386 -m 64M -cdrom rum.iso
```

For an archive made on another host, compare the uncompressed ISO's SHA256
with the verified `result.json`. ZIP bytes can differ between hosts because
source timestamps differ.

To validate the exact clean-build candidate without replacing local build
outputs, run the host check against its result directory. On Windows:

```powershell
py -3 tests/release-host-test.py --release build/release-check/<commit>-<time> --qemu qemu-system-i386.exe
```

On Linux/WSL, use `python3` and `--qemu qemu-system-i386`. This stages tracked
packaging sources and documentation in a temporary directory, packages the
validated ISO with that host's Python, verifies every ZIP member, and boots the
ISO extracted from that ZIP with native QEMU. It exercises `start`, `ps`, `kill`,
timed completion notices, recovery diagnostics and Snake. It never attaches a
mutable disk. Results and the archive are under the candidate directory's
`host-windows/` or `host-linux/` folder.

The clean check must have passed and the ISO hash must match. If only the README
or documentation changed while completing the release checklist, the host check
records both the tested code commit and the packaging commit. Any executable,
test or build-source change requires a new clean check. The working tree must
be committed for both checks.

## Try persistent files

Create and format a **new** image using [FAT16 setup](fat16.md). Attach it with
`make run DISK_IMAGE=/path/to/image.raw` or
`.\rum.ps1 run -DiskImage C:\path\image.raw`. In rum:

```text
/> write /disk/notes.txt saved on the island
/> cat /disk/notes.txt
saved on the island
```

Close QEMU, boot with the same image and read the file again. With QEMU stopped,
check it from the host:

```sh
mcopy -i /path/to/image.raw ::/NOTES.TXT -
mdir -i /path/to/image.raw ::/
```

Never edit or repair an image while it is attached to rum. FAT names use the
supported uppercase 8.3 spelling; lowercase paths resolve to that spelling.
RAM edits disappear on reboot, `/rum` stays read-only, and `/disk` is the
persistent mount. See [paths](filesystems.md), [file syscalls](filesystem-syscalls.md)
and [process syscalls](process-syscalls.md) for their public contracts.

## Disk errors and recovery

A successful mutation flushes file data, both FAT copies and directory metadata
in the [documented order](fat16.md#write-ordering-and-interruption). FAT writes
are not journaled or crash-atomic. Full media can also prevent copy-on-write
replacement of an existing file; deleting a closed file can free space.

After an uncertain write or flush error, the disk mount stops further I/O.
RAM files and `/rum` programs remain available. Use `recovery` for `diag`, `mem`
and the kernel console. Stop QEMU, preserve the failed image and inspect or
repair a disposable copy with host tools before mounting it again. A rejected
disk at boot does not prevent the normal userspace shell from starting.

## Publish the verified archive

Before tagging, set the release's displayed version and rerun the checks on
that committed source. Upload the verified `rum.zip`; `rum.iso` can also be a
separate download. Keep a mutable user disk separate. Record the source commit
and ISO SHA256 so the downloaded artifact can be compared with the tested one.
