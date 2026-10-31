# rum documentation

Start with [setup](setup.md), then [prepare a system disk](system-volume.md).
The ISO starts the kernel; the FAT16 disk mounted at `/` supplies `/RUM`
programs and persistent files. With no usable disk, rum opens recovery.

## Using rum

- [Shell and commands](shell.md)
- [Files and paths](filesystems.md)
- [FAT16 storage and recovery](fat16.md)
- [VMware and VirtualBox](virtual-machines.md)
- [Snake](snake.md)
- [Diagnostics](diagnostics.md)

## Building and development

- [Source layout](source-layout.md)
- [Memory layout](memory-layout.md) and [memory management](memory.md)
- [Heap and storage](storage.md)
- [Interrupts](interrupts.md) and [exceptions](exceptions.md)
- [Tasks](tasks.md), [scheduling](scheduling.md) and [console sessions](console-sessions.md)
- [User ABI](user-abi.md), [filesystem syscalls](filesystem-syscalls.md) and [process syscalls](process-syscalls.md)
- [Process tools](process-tools.md)
- [Block devices](block-devices.md)
- [Releases](release.md) and [roadmap](roadmap.md)

## Startup

GRUB loads the kernel. CPU setup establishes the boot stack, descriptor tables
and privilege-transition state. The kernel initializes display, serial output,
interrupt tables and platform devices, then reserves boot memory, initializes
paging and starts the heap and filesystem namespace.

The ATA driver probes the secondary IDE master. A supported FAT16 volume mounts
at `/` before the boot task receives its working-directory reference. Tasks
may also initialize without a filesystem so diskless recovery remains usable.
The kernel starts `/RUM/SHELL.ELF` in ring 3 and supervises restarts. There is
no boot module, RAM filesystem or automatic disk formatter.

IRQs only acknowledge hardware and update bounded state. Drawing, allocation,
filesystem I/O, commands and cleanup run in foreground/task context.

## Checks

`make test` runs the complete host/QEMU suite with four guest workers.
Use `TEST_JOBS=2` or `1` on a smaller host. `make test-host`, `test-user`,
`test-fs`, `test-fat16`, `test-fat16-write`, `test-userspace-shell` and
`test-system-disk` are focused checks. Logs and reports are under
`build/test-artifacts/`. Tests use disposable images, never a caller's disk.
