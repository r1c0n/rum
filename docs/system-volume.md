# System disk

The persistent FAT16 disk is mounted at `/`. `/RUM` contains the shell and
compiled commands on that same disk. There is no RAM filesystem or separate
read-only system volume.

The ISO contains the kernel, bootloader and recovery console. Keep it attached
for startup. A prepared disk is required to start the normal shell; missing,
unprepared or damaged disks leave rum in recovery.

## Prepare a new disk

In PowerShell:

```powershell
.\rum.ps1 build
.\rum.ps1 prepare-disk -DiskImage .\build\rum-system.raw
.\rum.ps1 run -DiskImage .\build\rum-system.raw
```

In Ubuntu/WSL:

```sh
make -j4
make prepare-disk DISK_IMAGE=build/rum-system.raw
make run DISK_IMAGE=build/rum-system.raw
```

Preparation requires mtools. It creates a new 16 MiB unpartitioned FAT16 image,
installs the programs built by `make user` into `/RUM`, and copies README.TXT
and WELCOME.TXT into the root. Supported sizes are 16 through 128 MiB. Use
`DISK_SIZE_MIB=32` with Make or `-DiskSizeMiB 32` with PowerShell to change it.
Existing paths, including symlinks, are refused. Normal startup never formats,
truncates, replaces or prepares an image. `create-disk` remains a separate
low-level helper that creates a blank raw image without a filesystem.

## Files and recovery

Paths are case-insensitive and directory listings use uppercase 8.3 names.
`/rum/shell.elf` and `/RUM/SHELL.ELF` identify the same program. Files anywhere
on the disk, including `/RUM`, persist across restarts. Back up the image
before changing system programs. Removing or damaging SHELL.ELF causes the
next startup to enter recovery. Read-only disks can run programs but reject
writes. See [FAT16](fat16.md) for interrupted-write limitations.

Recovery provides help, echo, diagnostics, memory information and Snake even
without a disk. Its file commands use the same disk root when available and
report an unavailable filesystem otherwise. Snake saves its best score in
`/SNAKE.DAT` when writable storage is available.

The normal shell handles `recovery` and `exit` without loading another
program, so they remain usable after disk failures. `recovery` closes the
session and enters the kernel console; `exit` requests a shell restart.
