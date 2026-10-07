# System volume

`/rum` contains the shell, command executables and example programs. It is a
read-only boot volume, available even when no disk is attached. Root files
remain mutable RAM files, and `/disk` is the optional persistent FAT16 volume.

```text
/
  readme.txt
  welcome.txt
  rum/
    shell.elf
    help.elf
    echo.elf
    cat.elf
    ls.elf
    cd.elf
    ...
  disk/
    ...
```

The normal shell only edits input, splits arguments, searches for executables
and waits for them. Command behavior lives in separate sources under
`user/programs/`. Every program uses public headers and syscalls; the user build
cannot include private kernel interfaces or link kernel objects. Drivers,
memory management and filesystem implementation remain kernel services.
The kernel recovery console and Snake engine remain kernel applications.

Commands search `/rum` first, then the current directory. Explicit paths bypass
this search, and the kernel supplies an optional `.elf` suffix. For example,
`cat readme.txt` launches `/rum/cat.elf` and reads `readme.txt` relative to the
shell's directory. `ls /rum` shows the installed programs. `cd`, `exit` and
`recovery` use restricted [session calls](process-syscalls.md) to affect the
waiting shell. Other child processes have independent working directories.

## Building and booting

`make user` builds and validates each ELF, then `scripts/pack-system.py` writes
`build/rum-system.img`. `make` also builds the kernel and ISO. Adding a program
requires its C source and a `USER_PROGRAMS` entry; there is no shell command
dispatch table to update. The image includes exactly that program list, so
old staged binaries cannot reappear after removing a program. Rebuild the ISO
to include the new system image.

The kernel ELF contains no userspace executable payloads. GRUB loads the kernel
and system image separately using `multiboot` and `module`. The ISO includes both,
so releases still only need `rum.iso` plus the accompanying documentation.
Direct ELF boots need both artifacts:

```sh
qemu-system-i386 -m 64M -kernel build/rum.elf -initrd build/rum-system.img
```

`make run-kernel` and `./rum.ps1 run-kernel` supply the module automatically.
Without a valid module, rum enters the RAM recovery shell. Normal boot never
creates, overwrites or formats a disk to install `/rum`. To update commands,
replace the system image by rebuilding rather than trying to write into `/rum`.

## Image format and ownership

The image is deterministic, little-endian and bounded to 1 MiB. Its 32-byte
header stores `RUMSYS1` plus NUL, version 1, total bytes, file count, entry size,
header size and a zero reserved word. It allows 1–64 files of at most 64 KiB.
Each 80-byte entry contains a 64-byte NUL-terminated, zero-padded name, a data
offset, a byte count and two zero reserved words. Names use the case-sensitive
RAM naming rules. Payloads follow the index in order, aligned to four bytes
with zero padding. Duplicate names, gaps, overlaps, arithmetic overflow,
unsupported layouts and trailing data are rejected before mounting.

The kernel copies the validated first Multiboot module before paging, so it
does not depend on the bootloader placing it inside rum's identity window.
PMM-owned contiguous low pages hold the copy for the entire boot; failed
mounting releases them. The original module remains reserved as boot data.
Filesystem IDs refer to immutable archive entries, and open references use the
common object table. `/rum` cannot be removed, masked by a RAM file or escaped
through `..`; use an absolute path to select another mount.

`make test-system-volume` checks malformed and missing modules in QEMU and
verifies that RAM recovery remains usable. The boot smoke tests compare the
owned copy with the host image and include its frames in the physical ledger.
