# rum documentation

Start with [setup](guides/setup.md), then [prepare a system disk](guides/system-volume.md).
The ISO starts the kernel; the FAT16 disk mounted at `/` supplies `/RUM`
programs and persistent files. With no usable disk, rum opens recovery.

## Using rum

- [Shell and commands](guides/shell.md)
- [Redirection and pipelines](guides/shell-workflows.md)
- [Background jobs](guides/jobs.md) and [process tools](guides/process-tools.md)
- [Files and paths](guides/filesystems.md)
- [File tools and workspace](guides/file-tools.md)
- [VMware and VirtualBox](guides/virtual-machines.md)
- [Snake](guides/snake.md)
- [Diagnostics](guides/diagnostics.md)

## Kernel

- [Memory layout](kernel/memory-layout.md) and [memory management](kernel/memory.md)
- [Executable loading and limits](kernel/executable-loading.md)
- [Heap and storage](kernel/storage.md)
- [Block devices](kernel/block-devices.md) and [FAT16 storage and recovery](kernel/fat16.md)
- [Kernel objects](kernel/objects.md)
- [System authority and recovery installation](kernel/system-authority.md)
- [Interrupts](kernel/interrupts.md) and [exceptions](kernel/exceptions.md)
- [Tasks](kernel/tasks.md), [scheduling](kernel/scheduling.md) and [console sessions](kernel/console-sessions.md)
- [Thread ownership, synchronization and ABI design](kernel/threading.md)

## Application development

- [Standalone SDK and application builds](sdk/sdk.md)
- [Application heap and C runtime](sdk/application-runtime.md)
- [User virtual memory](sdk/virtual-memory.md) and [application resource counts](sdk/resource-diagnostics.md)
- [User ABI](sdk/user-abi.md), [filesystem syscalls](sdk/filesystem-syscalls.md) and [process syscalls](sdk/process-syscalls.md)
- [Shared handles and child streams](sdk/shared-handles.md)
- [Typed object handles](sdk/object-handles.md) and [process handles](sdk/process-objects.md)
- [Choosing child handles](sdk/handle-inheritance.md)
- [Pipes and stream I/O](sdk/pipes.md)
- [Terminal API](sdk/terminal.md)

## Project development

- [Source layout](development/source-layout.md) and [testing](development/testing.md)
- [Doom compatibility probe](development/doom-compatibility.md)
- [Releases](development/release.md) and [roadmap](roadmap.md)

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

See [testing](development/testing.md) for worker limits, coverage and timing reports.
