# rum application SDK

Build static i386 applications without the rum source tree. This SDK targets
the 0.8 development interfaces. Typed I/O, process handles and explicit child
grants require the corresponding kernel features listed in `sdk.json`;
the documented 0.7 application interfaces remain supported. The older 0.6
release lacks required memory/runtime calls. `sdk.json` records the target,
interface version, source provenance,
required kernel features and SHA-256 hashes of the packaged files. SDK format
version 1 is separate from the OS release number and syscall ABI version 1.
`rum_spawn_with_objects` uses version 2 of the mapped-spawn packet; kernels
that only support its original version reject that packet with `EINVAL`.

`rum_resources` returns caller-local descriptor, typed-handle, unique-object
and table-reference counts. It initializes its 80-byte version-1 packet and
returns zero or a negative error. Type counts use `RUM_OBJECT_*` indices.
Aliases count as additional table references, not additional objects. No global
reference counts, tokens, names or addresses are exposed. This requires
`local-resource-counts-v1`; older kernels return `ENOSYS`.
`system-authority-v1` protects `/RUM` from userspace changes. Keep writable
application binaries/data in `/APPS`, `/HOME` or another ordinary directory;
system updates use the kernel recovery installer. Names and inherited writable
handles do not grant system-maintenance authority.
Git builds record the commit and whether sources were modified. A clean release
snapshot preserves that identity without Git; other source archives may report
unknown provenance. File hashes still describe the exact packaged contents.

## Requirements

Use Linux or Ubuntu in WSL on Windows, Python 3.9 or newer, a host C compiler
(`cc`), and the separate `i686-elf` GCC/binutils toolchain. The SDK does not
bundle a compiler. If using rum's toolchain, the prefix is the absolute path
ending in `.tools/cross/bin/i686-elf-`. Host GCC cannot replace the cross compiler.

## Build an application

Unpack the archive, copy `template/` into your own project directory, then:

```sh
export RUM_SDK='/path/to/rum-sdk'
make CROSS_PREFIX=/path/to/i686-elf-
```

Or use the build tool directly from your project:

```sh
python3 "$RUM_SDK/tools/build.py" --cross-prefix /path/to/i686-elf- --output build/example.elf main.c
```

On Windows, extract the ZIP with `Expand-Archive`, copy `template` to a new
project, and run its launcher:

```powershell
.\build.ps1 -Sdk 'F:\SDKs\rum-sdk' -CrossPrefix '/mnt/f/Projects/osdev/rum/.tools/cross/bin/i686-elf-'
```

The launcher builds inside Ubuntu/WSL. Supply `-Distro` for another distribution.
`-Sdk` is a Windows path; `-CrossPrefix` is the prefix visible inside WSL.
Paths containing spaces are supported. The tool emits a stripped `example.elf`,
a symbol-rich `example.debug.elf`, a linker map and intermediate objects. It
builds the host ELF validator from the supplied source with public headers only.
No kernel sources, private include paths or OS rebuild are required.

Use `--print-config` to obtain compiler/linker arguments as JSON. Additional C
flags can be supplied as `--cflag=-DNAME=value`. The supported runtime is linked
from `lib/librum.a` with `lib/crt0.o` and target `libgcc`.

## Install and run

Run `setup` in the guest to create `/APPS`, then stop the VM and back up its
disk. Copy the stripped ELF to `/APPS/EXAMPLE.ELF` using host FAT tools, then
boot rum and run:

```text
run /APPS/EXAMPLE.ELF
run /APPS/EXAMPLE.ELF /SDKTEST.BIN
```

The first command checks heap growth, recoverable failure and timing. The
second creates or replaces the named file and verifies its exact bytes.
The example also accepts `--fault` and `--spin` for recovery checks; Ctrl+C
cancels the latter. Existing disks are never updated implicitly by an SDK build.

## Application contract

Use `int main(int argc, char **argv)`. Arguments are decoded by the shell;
`argv[argc]` is null, with at most 32 arguments and 4096 string bytes including
terminators. The environment is empty. Returning from `main` flushes C output
streams and exits; faults and cancellation skip C cleanup but kernel resource
cleanup still runs. Launch and wait operations use public versioned packets.

Files are on the FAT16 root `/`; `/RUM` holds system programs. Paths are
case-insensitive 8.3 names, with a 256-byte path capacity including NUL. Standard
descriptors 0–2 may refer to console, files or pipes. There are 32 descriptors
per process, including standard streams. C `FILE` streams are unbuffered,
preserve binary bytes and expose separate EOF/error indicators. Check `fflush`
and `fclose` results for write errors. Flush has the kernel filesystem's
ordering guarantees; it does not add journaling.

Typed object handles use a separate 32-entry table and opaque unsigned tokens.
`rum_object_query`, `rum_object_duplicate` and `rum_object_close` operate on
handles granted by trusted resource services; ordinary `rum_open` still returns
a descriptor. Query needs QUERY rights and duplication needs DUPLICATE plus a
subset of the source grant. Entries are non-inheritable unless explicitly
marked `RUM_OBJECT_INHERIT`. See [the public packet definitions](include/rum/abi/object.h).
These calls require the 0.8 development kernel; the existing descriptor calls
and their standard-stream, shared-offset and close-on-spawn rules are preserved.

Executables are static little-endian i386 ELF32 ET_EXEC, at most 16 MiB on disk
with 16 program headers. LOADs must occupy distinct ordered pages in the user
program window. The entry must be in file-backed executable bytes. The image,
64 KiB stack and dynamic memory share a 16 MiB committed-page budget; physical
RAM can run out earlier. Stack guards and private address spaces are enforced.
Non-PAE paging has no NX bit. x87/MMX/SSE instructions are unsupported: keep the
supplied integer-only compiler flags. Dynamic linking, TLS and constructors
are not supported.

`malloc`, `calloc`, `realloc` and `free` use private regions and 16-byte
alignment. Zero-size allocation returns null; `realloc(pointer, 0)` frees it.
Failed growth preserves the original allocation. Empty arenas return to the
kernel; fragmented arenas may retain backing pages. `errno` is process-local,
uses rum values and is set by C wrappers; direct `rum_*` calls return negative
errors instead. The runtime currently assumes one userspace thread per process.

The C subset includes byte/string operations, file streams and integer
`printf`/`snprintf` formatting. Floating-point formatting, scanning, locale,
environment variables, signals and `atexit` are not implemented. Public headers
declare the complete supported surface. Use `rum_monotonic_ms` and `rum_sleep`
for elapsed time and relative waits; there is no wall clock or process CPU clock.

ABI version 1 and individual packet versions describe the public wire formats;
they are not a promise that every earlier kernel implements every operation.
Match the required feature set in `sdk.json` to the kernel being used. Unknown
syscalls fail with `ENOSYS`; unknown packet versions/flags are rejected.

## Optional Doom compatibility probe

The SDK includes a probe for DoomGeneric revision
`e257a09527062bfb986b6e5e0780839b6cbd5b71`. From a separate work directory:

```sh
python3 "$RUM_SDK/tools/probe-doom.py" --source ./doomgeneric-source --output ./doom-report --cross-prefix /path/to/i686-elf- --fetch
```

Only `--fetch` downloads source. Existing checkouts must be clean and match the
pinned revision; the probe never changes them. Omit `--fetch` for offline runs.
No WAD data is downloaded or bundled. Upstream source is GPL-2.0-or-later; keep
its license and source obligations when distributing combined binaries.

The report separates engine compilation, partial/full link diagnostics and a
successfully linked `DOOMIO.elf`. The latter uses the unmodified upstream zone
allocator and standard-C WAD backend, with a small rum adapter. It checks a
synthetic single-lump test WAD; it does not start the game. Probe-only
`inttypes.h` and `strings.h` declaration bridges are explicitly reported and
are not added to the normal application include path.

Compile gaps remain in the general C/POSIX and math surface. Graphics, complete
key press/release delivery and game/config path adaptation remain port work.
Successful SDK packaging or the headless probe does not declare Doom playable.
