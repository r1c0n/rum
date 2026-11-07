# Setup and build commands

rum builds under Ubuntu 24.04, either directly or through WSL 2 on Windows.
The local `i686-elf` toolchain lives in `.tools/cross/`; build products live in
`build/`. Both are excluded from Git.

## Windows

Install WSL 2 with Ubuntu and put Windows QEMU on PATH. The launcher uses
WSL's Python, Make and cross-compiler; a Windows Python installation is not
required. Interactive boot uses Windows QEMU. Tests use Linux QEMU inside WSL.

```powershell
.\rum.ps1 setup -Jobs 4
.\rum.ps1 doctor
.\rum.ps1 build -Jobs 4
.\rum.ps1 prepare-disk -DiskImage build\rum-system.raw
.\rum.ps1 run -DiskImage build\rum-system.raw
```

Use `-Distro <name>` when the distribution is not named `Ubuntu`. Use
`-Qemu 'E:\qemu\qemu-system-i386.exe'` to select a Windows executable for boot.
For `test` and `release-check`, `-Qemu` instead names a Linux executable in WSL.
Paths passed to `-DiskImage` and `-Output` are relative to your current
PowerShell directory, even when invoking the launcher from elsewhere.

If script execution is blocked, allow it for this invocation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\rum.ps1 help
```

## Linux and WSL

```sh
./rum setup --jobs 4
./rum doctor
./rum build --jobs 4
./rum prepare-disk --disk build/rum-system.raw
./rum run --disk build/rum-system.raw
```

`./rum` and `rum.ps1` use the same command implementation in `scripts/rum.py`.
Run `./rum help` or `.\rum.ps1 help` for available actions and options. Without
an action, both launchers run rum. Python 3.10 or newer is required.

Setup installs Ubuntu dependencies using `sudo`, builds Binutils 2.45 and GCC
15.2.0 for `i686-elf`, and checks the environment. It includes GRUB BIOS
modules, xorriso, Mtools, dosfstools, QEMU, qemu-img, GDB and Git. Setup does
not prepare or attach a disk. Normal startup needs an explicitly prepared
system disk; the ISO alone opens recovery.

Toolchain downloads use GNU's HTTPS servers. Compilation defaults to four
jobs. Interrupted builds can be resumed with `./rum toolchain --jobs 2`.
Sources and build logs live under `/tmp/rum-toolchain-<uid>/`, in a directory
specific to the compiler versions and installation path. Set
`RUM_TOOLCHAIN_BUILD_DIR` to choose another Linux directory. Keep temporary
compiler builds on Linux storage for better WSL performance.

An existing cross-toolchain can be selected without modifying your profile:

```sh
./rum build --cross-prefix i686-elf-
./rum doctor --cross-prefix i686-elf-
# The underlying Make interface remains available:
make -j4 CROSS_PREFIX=i686-elf-
```

## Commands

| Action | PowerShell | Linux / WSL |
| --- | --- | --- |
| Build kernel, programs and ISO | `.\rum.ps1 build -Jobs 4` | `./rum build -j 4` |
| Build programs only | `.\rum.ps1 user` | `./rum user` |
| Run the ISO | `.\rum.ps1 run -DiskImage build\rum-system.raw` | `./rum run --disk build/rum-system.raw` |
| Run the kernel ELF | `.\rum.ps1 run-kernel -DiskImage build\rum-system.raw` | `./rum run-kernel --disk build/rum-system.raw` |
| Complete test suite | `.\rum.ps1 test -Jobs 4 -Workers 4` | `./rum test -j 4 --workers 4` |
| Host tests only | `.\rum.ps1 test -Suite host` | `./rum test --suite host` |
| Shell tests | `.\rum.ps1 test -Suite shell` | `./rum test --suite shell` |
| ZIP with ISO and docs | `.\rum.ps1 package` | `./rum package` |
| Fresh VMware/VirtualBox bundle | `.\rum.ps1 vm-images -Output build\vm-images-new` | `./rum vm-images --output build/vm-images-new` |
| Clean compiler outputs | `.\rum.ps1 clean` | `./rum clean` |
| Clean-source release validation | `.\rum.ps1 release-check` | `./rum release-check` |

Build jobs range from 1 to 64. Test workers range from 1 to 4 and control
independent QEMU guests; they do not control compiler parallelism. On Linux,
`JOBS` and `TEST_JOBS` provide defaults. PowerShell defaults both to four.
See [testing](testing.md) for focused groups and reports.

Boot normally rebuilds first. `-NoBuild` / `--no-build` uses existing artifacts.
`-MemoryMiB 256` / `--memory 256` changes guest RAM; the default is 64 MiB.
`-DryRun` / `--dry-run` validates options and prints commands without building,
launching, formatting or deleting anything:

```powershell
.\rum.ps1 run -DiskImage build\rum-system.raw -NoBuild -DryRun
```

```sh
./rum run --disk build/rum-system.raw --no-build --dry-run
```

Make targets remain available, including `make -j4`, `make user`, `make test
TEST_JOBS=4`, `make run DISK_IMAGE=...`, `make clean` and subsystem tests.
The root Makefile includes configuration, kernel, userspace, test and workflow
rules from `mk/`; add source files to the appropriate list there.

## Files and cleanup

A build creates `build/rum.elf`, `build/rum.map`, `build/rum.iso`, program ELFs
under `build/user/system/`, and debug ELFs/maps under `build/user/debug/`.
`build/user/programs.txt` lists programs for disk installation. Rebuilding does
not update an existing system disk. See [system disks](system-volume.md).

`clean` removes compiler objects, dependencies, generated linker/header files,
host test binaries, staged kernels and built ISOs. It preserves disk images,
`build/disks/`, VM bundles, release reports, test evidence and unrelated files.
It does not recursively delete `build/` or remove `.tools/`. Create a new disk
path for a fresh installation; existing paths are refused. A blank image made
with `create-disk` has no FAT filesystem or programs.

`package` builds first and atomically replaces `rum.zip`. Use `-Output` /
`--output` for a different ZIP path, or `-NoBuild` / `--no-build` to package an
existing ISO. The output parent must exist. VM bundles require a new directory
and include independently writable raw, VMDK and VDI copies. See
[VMware and VirtualBox](virtual-machines.md) and [release checks](release.md).

## Debugging

```powershell
.\rum.ps1 debug -DiskImage build\rum-system.raw
```

```sh
./rum debug --disk build/rum-system.raw
# In another terminal:
gdb build/rum.elf
```

```gdb
target remote localhost:1234
break kernel_main
continue
```

The VM starts paused with a GDB server on port 1234. If WSL cannot reach
Windows QEMU through localhost, use `./rum debug` inside WSL instead.
`panic` boots the isolated invalid-opcode test kernel for inspecting diagnostics.

## Troubleshooting

- **Missing tools:** run `doctor`, then `setup` if dependencies need installing.
- **Unknown WSL distribution:** check `wsl --list --verbose` and pass `-Distro`.
- **Missing Windows QEMU:** add its directory to PATH or supply `-Qemu`.
- **Compiler build runs out of memory:** lower `-Jobs` / `--jobs` to 2 or 1.
- **Unexpected stale outputs:** run `clean`, then `build`. Disks are preserved.
- **Old commands after rebuilding:** programs are loaded from the attached
  disk. Prepare a new disk, or back up and deliberately update `/RUM` while
  the VM is stopped.
- **Recovery opens instead of the normal shell:** attach a prepared system
  disk at secondary IDE master; check that it contains `/RUM/SHELL.ELF`.
- **Keyboard input is missing:** click the QEMU display. Use Ctrl+Alt+G to
  release capture.
- **A test failed:** inspect `build/test-artifacts/suite/result.json` and the
  adjacent logs. Do not run two full suites in one checkout at the same time.
