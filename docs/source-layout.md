# Source layout

The source tree separates kernel services, CPU-specific code, PC platform
support, device drivers, and user programs. Build outputs remain under `build/`;
the bootable artifacts are still `rum.elf`, `rum.iso`, and `rum-system.img`.

| Directory | Responsibility |
| --- | --- |
| `boot/grub/` | GRUB menus and boot configuration |
| `arch/i386/` | CPU setup, GDT/TSS, exception and syscall entry, context assembly, paging, and kernel linker script |
| `kernel/core/` | Initialization in `init.c` and freestanding memory primitives |
| `kernel/process/` | Task scheduling, process lifecycle, handles, ELF loading, and syscall dispatch |
| `kernel/mm/` | Physical allocation and kernel heap |
| `kernel/io/` | Common block-device operations and request serialization |
| `kernel/fs/` | Paths, mounts, retained objects, RAM/system volumes, and FAT16 |
| `kernel/recovery/` | Kernel recovery command loop and current Snake engine |
| `kernel/debug/` | Ownership snapshots, diagnostics, and panic reports |
| `kernel/include/rum/` | Private kernel interfaces and architecture/resource contracts |
| `hal/pc/` | Legacy PC IRQ routing, PIC controller, and PIT timing |
| `drivers/display/` | VGA text-console implementation |
| `drivers/serial/` | Serial-console device implementation |
| `drivers/input/` | PS/2 keyboard device and scan-code decoder |
| `drivers/storage/` | ATA PIO storage driver |
| `sdk/` | Public headers, application runtime, linker script, examples, and validator |
| `system/` | Userspace shell and standalone command programs |
| `apps/` | Application entry points |
| `assets/ramfs/` | Embedded RAM-file payloads |
| `tests/` | Host checks, test backends, guest fixtures, and QEMU automation |
| `scripts/` | Toolchain, disk-image, build, packaging, and test orchestration |

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

The userspace Snake entry is `apps/snake/main.c`. It currently invokes the
kernel game through the console ABI. The game engine remains under
`kernel/recovery/` until the public input and terminal interfaces can support
the full userspace port.

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

The Makefile keeps explicit kernel source groups: `CORE_SOURCES`,
`PROCESS_SOURCES`, `MM_SOURCES`, `IO_SOURCES`, `FS_SOURCES`, `HAL_SOURCES`,
`DRIVER_SOURCES`, `RECOVERY_SOURCES`, `DEBUG_SOURCES`, and `ARCH_SOURCES`.
Add new implementation files to their owning group.

User executables have an explicit manifest as well:

| Source | Makefile list | Installed name |
| --- | --- | --- |
| `system/shell/main.c` | Built-in shell entry | `shell.elf` |
| `system/commands/name.c` | `SYSTEM_COMMANDS` | `name.elf` |
| `apps/name/main.c` | `APP_PROGRAMS` | `name.elf` |
| `sdk/examples/name.c` | `SDK_EXAMPLES` | `name.elf` |

Names must be unique across these groups. Source discovery does not implicitly
install programs. The manifest controls the read-only system archive, while
the user-ELF checks verify that each source program has a validated executable.

Program objects live under `build/user/objects/`; debug ELFs and maps remain in
`build/user/debug/`, and stripped runtime ELFs remain in `build/user/system/`.
The normal `make`, `make user`, and PowerShell wrapper commands are unchanged.

## Checking layout changes

Run `make test-user` after changing public includes, runtime rules, or program
lists. Run the relevant host and QEMU fixtures for moved kernel code, followed
by `make test` before opening a pull request.

For a build with no stale objects or dependency files, use the
[clean snapshot workflow](release.md). It builds committed sources in a fresh
temporary directory and preserves existing developer disks under `build/`.
