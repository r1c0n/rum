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
SDK runtime and every program in the explicit Makefile manifest. Debug ELFs and
maps go to `build/user/debug/`; stripped assets go to `build/user/system/` and
are packed into `build/rum-system.img`.

Add an application at `apps/name/main.c` and add its name to `APP_PROGRAMS`.
For a small API example, add `sdk/examples/name.c` and list it in `SDK_EXAMPLES`.
See [User ABI and ELF programs](../docs/user-abi.md) for complete build, launch,
memory, argument, and error contracts.

User programs compile against this public tree and generated ABI copies only.
The kernel uses the same canonical ABI definitions alongside its private
`kernel/include/rum/` headers. Never copy kernel-private structures into the SDK
or include private headers from an application.

The current build is integrated with the repository Makefile. The host validator
also consumes kernel resource-policy headers. A standalone SDK export, library
archive, and external-project template are separate work; this source layout
does not yet provide an independently installable development package.
