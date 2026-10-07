# rum documentation

rum is a small 32-bit x86 hobby operating system. This documentation covers
building it, using the shell, and understanding the kernel well enough to make
changes without breaking its memory and interrupt-safety rules.

## Start here

1. Follow [Setup](setup.md) to install the cross-compiler, GRUB tools, and QEMU.
2. Run `./rum.ps1 run` from PowerShell or `make run` from Ubuntu.
3. Use the [Shell guide](shell.md) to explore files, diagnostics, and Snake.
4. Read [Debugging](setup.md#debugging) before changing low-level startup or
   exception code.

If you only want to try the release ISO, no compiler is required:

```sh
qemu-system-i386 -m 64M -cdrom rum.iso
```

## What works today

The normal boot provides a VGA text console, a PS/2 keyboard, a userspace shell,
temporary RAM files, diagnostics, and ASCII Snake. The kernel has physical and
virtual memory management, guarded task stacks, cooperative scheduling, and a
controlled double-fault path. Prepared processes enter ring 3 through the
production interrupt-return path, and a user exception terminates only that
process while preserving its fault record for the parent. The production
`int 0x80` dispatcher provides process exit, PID lookup, checked console/file
input and output, directory listing, and working-directory operations.
The shell can launch one foreground user process, wait for its
result, report its signed status or fault, and cancel it with Ctrl+C.

Stripped user ELF programs live in a separate read-only boot volume at `/rum`. The
kernel loader validates them again, builds private mappings and an initial user
stack, and hands the image to the process system. The shell and its commands
are individual userspace executables. RAM-file changes
disappear on reboot; disk-file changes persist.

An optional secondary IDE disk exposes raw sector I/O to the kernel. Boot mounts
supported FAT16 volumes at `/disk`, with writes available to kernel callers and
user programs on writable devices. Shell commands use all mounts, including
directories, file creation and ELF launch.

## User guides

| Guide | Use it for |
| --- | --- |
| [Setup](setup.md) | Installing dependencies, building, running, and attaching GDB |
| [Shell](shell.md) | Commands, editing rules, RAM-file examples, and input limitations |
| [System volume](system-volume.md) | `/rum`, standalone commands, build artifacts and boot-image validation |
| [ASCII Snake](snake.md) | Controls, scoring, and score-file behavior |
| [Heap and RAM files](storage.md) | File limits, embedded files, and the kernel storage APIs |
| [Raw disks and block devices](block-devices.md) | Image creation, safe attachment, sector I/O, ATA limits and errors |
| [FAT16 disks](fat16.md) | Image setup, file and directory operations, limits, flush ordering and repair expectations |
| [Building a release](release.md) | Clean builds, disk/session checks, packaging, persistence and recovery |
| [Kernel diagnostics](diagnostics.md) | Reading `diag` output and investigating a panic |

## Kernel guides

| Guide | Use it for |
| --- | --- |
| [Boot, exceptions, and CPU state](exceptions.md) | GDT, TSS, IDT, interrupt frames, user CPU policy, and double faults |
| [Device interrupts](interrupts.md) | PIC routing, PIT ticks, keyboard input, and IRQ restrictions |
| [Kernel tasks and process records](tasks.md) | Scheduling, guarded stacks, events, process publication, and cleanup |
| [Filesystems and paths](filesystems.md) | Backend operations, mounts, naming, access, retained objects, and working directories |
| [Memory management](memory.md) | Multiboot memory discovery, physical pages, paging, and checked user access |
| [Memory map and ownership](memory-layout.md) | Virtual ranges, limits, and who must release each resource |
| [User ABI and ELF programs](user-abi.md) | Building user code and following the public syscall, stack, and ELF contracts |
| [Filesystem syscalls](filesystem-syscalls.md) | Opening files from userspace, directory iteration, seek, paths and error handling |
| [Process syscalls](process-syscalls.md) | Foreground launch, child results, console ownership and cancellation |

The [roadmap](roadmap.md) describes planned work: multitasking and process
control for 0.5.0, then shell workflows and userspace tools for 0.6.0. The guides
above describe the interfaces available today.

## Boot flow

1. GRUB loads `rum.elf` and `rum-system.img`, then supplies the Multiboot v1 information structure.
2. `_start` establishes the boot stack, GDT, segment registers, TSS, and CPU
   policy before calling `kernel_main`.
3. The kernel starts VGA and COM1 output, installs the IDT, and configures the
   PIC, PIT, and PS/2 controller.
4. The memory manager reserves the kernel and boot data. The kernel validates
   the system image and copies it into owned low physical pages before paging.
   It builds paging, protects
   kernel code and constants, and leaves page zero unmapped.
5. The heap and RAM filesystem start, embedded files are copied into RAM, and
   the task system allocates guarded idle and double-fault stacks.
   The system image mounts read-only at `/rum`.
   The ATA driver probes the optional secondary disk and mounts supported FAT16
   volumes without writing the image; later kernel filesystem calls can mutate them.
6. IRQ0 and a detected keyboard are unmasked. The kernel starts `/rum/shell.elf` in
   ring 3 and supervises restarts, with the kernel shell available for recovery.
   User commands use syscalls; idle sleeps with
   `sti; hlt` whenever no work is ready.

Interrupt handlers only acknowledge hardware, update bounded state, queue
input, and signal work. Commands, drawing, memory allocation, blocking, and
resource cleanup happen in foreground task context.

## Checking a change

Run the narrowest relevant check while developing, then run the complete suite
before opening a pull request:

```sh
make test-host   # C tests for portable kernel components
make test-user   # User ELF and public-header validation
make test-userspace-shell # Ring-3 commands, child launch and recovery
make test-block  # Block API, ATA driver and disposable QEMU disk tests
make test-fs     # Paths, filesystem backends and process working directories
make test-fat16  # Host FAT images, malformed volumes and guest byte comparisons
make test-fat16-write # Writable FAT16, interrupted writes and host FAT validation
make test-package # Release archive and embedded-ISO validation
make test-release-integration # Storage failures, full media, reboot and packaged ISO
make test        # Complete host, package, and QEMU suite
```

Every isolated QEMU case runs with 16 and 64 MiB. Boot coverage also exercises
the GRUB ISO and direct ELF, with and without a foreground process, across 16,
64, 256, and 1152 MiB where applicable. QEMU logs, screenshots, register dumps,
and ownership reports are written to `build/test-artifacts/`. These files are
generated diagnostics and should not be committed.

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) for
new commits: `type(scope): description`, for example
`feat(ata): add secondary disk reads` or `fix(block): reject overflowing ranges`.
Use `test`, `docs`, `refactor` and `build` for changes in those areas. Commit
working pieces as they become reviewable; keep each pull request focused on one
roadmap phase.
