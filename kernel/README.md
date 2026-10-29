# Kernel source layout

Kernel implementations are grouped by responsibility. Kernel-private interfaces
live in `kernel/include/rum/`; public application headers and shared ABI
definitions live in `sdk/include/rum/`. Both use `<rum/...>` includes, but only
the kernel build can see the private tree.

| Directory | Responsibility |
| --- | --- |
| `core/` | Kernel initialization and freestanding memory primitives |
| `mm/` | Physical allocation and the kernel heap |
| `process/` | Tasks, processes, ELF loading, and syscalls |
| `io/` | Common block-device API and request serialization |
| `fs/` | Filesystem implementations, paths, mounts, and storage backends |
| `recovery/` | Kernel recovery shell and the current kernel Snake engine |
| `debug/` | Runtime snapshots and panic diagnostics |
| `include/rum/` | Kernel-private interfaces, CPU contracts, and resource limits |

Architecture-specific CPU setup, interrupt entry, and paging remain under
`arch/i386/`. PC interrupt routing, PIC, and PIT code live in `hal/pc/`.
Device implementations live under the top-level `drivers/` tree. Userspace
programs live under `system/`, `apps/`, and `sdk/examples/`, with their shared
runtime in `sdk/lib/`.

New code should live with the subsystem that owns its state. Hardware drivers
belong in the top-level `drivers/`, while filesystem formats and namespace code
belong in `kernel/fs/`. Applications use public SDK headers and syscalls.
See the [source layout guide](../docs/source-layout.md) for build rules and
application entry points.
