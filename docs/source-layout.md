# Source layout

The source tree separates kernel services, CPU-specific code, PC platform
support, device drivers, and user programs. Build outputs remain under `build/`;
the kernel is built as `rum.elf` and packaged in `rum.iso`; an explicitly prepared FAT16 disk supplies programs and files.

| Directory | Responsibility |
| --- | --- |
| `boot/grub/` | GRUB menus and boot configuration |
| `arch/i386/` | CPU setup, GDT/TSS, exception and syscall entry, context assembly, paging, and kernel linker script |
| `kernel/core/` | Initialization in `init.c` and freestanding memory primitives |
| `kernel/process/` | Task scheduling, process lifecycle, referenced console sessions, handles, ELF loading, and syscall dispatch |
| `kernel/mm/` | Physical allocation and kernel heap |
| `kernel/io/` | Block-device operations, request serialization, pipes and interactive terminal leases |
| `kernel/fs/` | Paths, mounts, retained objects, the persistent FAT16 root |
| `kernel/recovery/` | Recovery command loop and diskless Snake controller |
| `kernel/debug/` | Ownership snapshots, diagnostics, and panic reports |
| `kernel/include/rum/` | Private kernel interfaces and architecture/resource contracts |
| `hal/pc/` | Legacy PC IRQ routing, PIC controller, and PIT timing |
| `drivers/display/` | VGA text-console implementation |
| `drivers/serial/` | Serial-console device implementation |
| `drivers/input/` | PS/2 keyboard device and scan-code decoder |
| `drivers/storage/` | ATA PIO storage driver |
| `sdk/` | Public headers, application runtime, linker script, examples, and validator |
| `system/` | Userspace shell and standalone command programs |
| `system/lib/` | Private userspace helpers shared by command ELFs, including bounded file copying |
| `apps/` | Application entry points |
| `assets/disk/` | Initial system-disk documentation |
| `tests/` | Host checks, test backends, guest fixtures, and QEMU automation |
| `scripts/` | Toolchain, disk-image, build, packaging, and test orchestration |
| `mk/` | Make configuration and separate kernel, userspace, test and workflow rules |

## Choosing a subsystem

CPU register policy, saved-frame layouts, and architecture entry/return belong
under `arch/i386/`. PC interrupt-controller and timer implementations belong
under `hal/pc/`. Device-specific storage, input, display, or serial code belongs
in the matching `drivers/` directory.

The block interface belongs in `kernel/io/`; ATA register access belongs in
`drivers/storage/ata.c`; FAT interpretation belongs in `kernel/fs/`. Keep format
logic separate from device commands. Header names such as `<rum/block.h>` stay
the same when an implementation moves.

The HAL directory groups the current PC implementations. It does not introduce
a new dynamic driver model, hardware target, or interrupt ABI. Existing private
interfaces remain in use, including the legacy PIC routing contract.

The userspace Snake entry is `apps/snake/main.c`, with its own loop, rendering,
score I/O and public terminal calls. Its pure `model.c`/`model.h` rules are
compiled independently into the user ELF and kernel recovery controller.
No private kernel headers or cross-privilege game state are shared.

`system/shell/main.c` owns the interactive prompt and plain command launches.
`parse.c` validates bounded workflow syntax, and `pipeline.c` prepares streams,
launches stages and collects their results. These files use SDK interfaces;
filesystem, pipe and console ownership remain kernel services.
`kernel/process/jobs.c` retains bounded job results after task reaping; `task.c`
enforces controller authority, waits and cancellation, while `session.c` owns
console input leases. The standalone `jobs`, `fg` and `wait` programs live in
`system/commands/` and include public SDK headers only.

## Header and runtime boundaries

`sdk/include/rum/abi/` is the source of truth for public ABI packets and constants.
Kernel services and user programs include the same definitions. The build
copies those headers into `build/user/include/rum/abi/` for application staging.
Do not maintain a second hand-edited ABI tree.

Kernel code compiles with `kernel/include/` and `sdk/include/`. User code compiles
with `sdk/include/` and generated public headers; kernel-private headers and
objects are absent from that build. The negative include tests in
`tests/user-elf-test.py` enforce this boundary.

Startup and runtime objects are built under `build/sdk/lib/`. The linker source
`sdk/linker/user.ld` generates `build/sdk/linker/user.ld`. A user ELF links those
runtime objects and target `libgcc`, independently of kernel objects and the
host C library. See [SDK sources](../sdk/README.md) for the current interface.

## Registering source files

`mk/kernel.mk` keeps explicit kernel source groups: `CORE_SOURCES`,
`PROCESS_SOURCES`, `MM_SOURCES`, `IO_SOURCES`, `FS_SOURCES`, `HAL_SOURCES`,
`DRIVER_SOURCES`, `RECOVERY_SOURCES`, `DEBUG_SOURCES`, and `ARCH_SOURCES`.
Add new implementation files to their owning group.

`mk/user.mk` lists user executables explicitly:

| Source | Program list | Installed name |
| --- | --- | --- |
| `system/shell/main.c` | Built-in shell entry | `shell.elf` |
| `system/commands/name.c` | `SYSTEM_COMMANDS` | `name.elf` |
| `apps/name/main.c` | `APP_PROGRAMS` | `name.elf` |
| `sdk/examples/name.c` | `SDK_EXAMPLES` | `name.elf` |

Names must be unique across these groups. Source discovery does not implicitly
install programs. The manifest controls installation into `/RUM` on a new disk, while
the user-ELF checks verify that each source program has a validated executable.

Program objects live under `build/user/objects/`; debug ELFs and maps remain in
`build/user/debug/`, and stripped runtime ELFs remain in `build/user/system/`.

`rum.ps1` adapts PowerShell paths and selects Windows QEMU; `rum` starts Python
on Linux. Both delegate workflows to `scripts/rum.py`. `mk/config.mk` owns
compiler flags and tool defaults, `mk/tests.mk` owns fixtures and test targets,
and `mk/commands.mk` exposes the workflow commands through Make. Keep QEMU
device layout in `scripts/run-qemu.py`, rather than duplicating it in launchers.
The normal `make`, `make user`, and PowerShell wrapper commands are unchanged.

## Checking layout changes

Run `make test-user` after changing public includes, runtime rules, or program
lists. Run the relevant host and QEMU fixtures for moved kernel code, followed
by `make test` before opening a pull request.

For a build with no stale objects or dependency files, use the
[clean snapshot workflow](release.md). It builds committed sources in a fresh
temporary directory and preserves existing developer disks under `build/`.
