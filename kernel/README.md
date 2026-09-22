# Kernel source layout

Kernel implementations are grouped by responsibility. Public interfaces remain
under `include/rum/`, so moving an implementation does not change its include
path or expose private details to user programs.

| Directory | Responsibility |
| --- | --- |
| `core/` | Kernel entry and freestanding memory primitives |
| `drivers/` | VGA, serial, PIT, and PS/2 device support |
| `mm/` | Physical allocation and the kernel heap |
| `process/` | Tasks, processes, ELF loading, and syscalls |
| `fs/` | Filesystem implementations and storage backends |
| `ui/` | Kernel shell and interactive programs |
| `debug/` | Runtime snapshots and panic diagnostics |

Architecture-specific CPU setup, interrupt entry, and paging remain under
`arch/i386/`. Freestanding user programs and their runtime remain under `user/`.

New code should live with the subsystem that owns its state. Hardware drivers
belong in `drivers/`, while filesystem formats and namespace code belong in
`fs/`. Keep architecture-independent interfaces in `include/rum/` and avoid
including kernel-private headers from `user/`.
