# rum SDK sources

This directory contains the public application interface and the code used to
build rum programs. Applications include `<rum/user.h>`, link with the rum
runtime, and access operating-system services through syscalls.

| Directory | Contents |
| --- | --- |
| `include/rum/abi/` | Canonical fixed-width syscall packets, constants, and executable contracts shared with the kernel |
| `include/rum/` | Public runtime declarations and command helpers |
| `lib/` | Startup assembly, syscall entry/wrappers, and shared program helpers |
| `linker/` | Static i386 ELF linker script |
| `examples/` | Hello, exit-status, fault, cancellation, and input examples |
| `tools/` | Host-side ELF validator used before installing an executable |

From the repository root, run `make user` or `./rum.ps1 user`. This builds the
SDK runtime and every program listed in `mk/user.mk`. Debug ELFs and
maps go to `build/user/debug/`; stripped assets go to `build/user/system/` and
are listed in `build/user/programs.txt`. `prepare-disk` installs that manifest
into `/RUM` on a newly created FAT16 image. Existing disks are not changed by a build.

Add an application at `apps/name/main.c` and add its name to `APP_PROGRAMS`.
For a small API example, add `sdk/examples/name.c` and list it in `SDK_EXAMPLES`.
See [User ABI and ELF programs](../docs/user-abi.md) for complete build, launch,
memory, argument, and error contracts.

The runtime provides independent `rum_spawn`, `rum_wait`, `rum_terminate`,
`rum_sleep` and `rum_process_list` calls. See [Process syscalls](../docs/process-syscalls.md)
for packet layouts and [Process tools](../docs/process-tools.md) for shell examples.

`rum_duplicate` and `rum_spawn_with_handles` provide shared open descriptions
and explicit child stream mappings. See [shared handles](../docs/shared-handles.md)
for standard-stream replacement, close-on-spawn flags and cleanup rules.

`rum_pipe` creates bounded read/write endpoints for those mappings. See
[pipes and stream I/O](../docs/pipes.md) for blocking, atomic writes, EOF,
broken pipes and cancellation behavior.

Use `rum_open` CREATE/TRUNCATE/APPEND flags for output files and `rum_isatty`
to choose console display or byte-preserving stream output. The shell's
[workflow guide](../docs/shell-workflows.md) documents these contracts and its
foreground pipeline scope.

The [job API](../docs/jobs.md) exposes session-scoped result snapshots, waits,
foreground transfer and collection. Only the initial shell creates and joins
jobs; marked foreground command programs may manage that shell's jobs. All
operations use versioned fixed-width packets and public identifiers.

The [terminal API](../docs/terminal.md) adds capability queries, colored text
cells, cursor control and bounded timed character input. Interactive state
requires foreground ownership and is restored automatically on exit or fault.
`apps/snake/` demonstrates a complete game using public syscalls and an
application-local model, without kernel headers or device access.

User programs compile against this public tree and generated ABI copies only.
The kernel uses the same canonical ABI definitions alongside its private
`kernel/include/rum/` headers. Never copy kernel-private structures into the SDK
or include private headers from an application.

The current build is integrated with the repository Makefile. The host validator
also consumes kernel resource-policy headers. A standalone SDK export, library
archive, and external-project template are separate work; this source layout
does not yet provide an independently installable development package.
