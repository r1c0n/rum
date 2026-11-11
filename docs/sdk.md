# Building rum applications

Use `rum_resources` for [caller-local object and handle counts](resource-diagnostics.md).
It reports aliases and object types without pointers or global ownership data.

The standalone SDK contains public headers, a startup object, `librum.a`, a
processed linker script, the ELF validator source, an application builder and
a project template. It does not contain kernel sources, a disk image or the
cross compiler. This SDK targets the 0.8 development interfaces, including
[typed object handles and I/O](object-handles.md). Export a descriptor with
`rum_object_from_descriptor` to use the typed read/write, seek, directory and
flush wrappers without opening another description. [Process handles](process-objects.md)
provide explicit query/wait/termination rights and retained status. Existing 0.7
applications retain their documented stream and memory behavior.
`rum_spawn_with_objects` selects [child handles and reduced grants](handle-inheritance.md)
alongside stream mappings. Older version-1-only kernels reject its version-2
spawn packet with `EINVAL`. New object calls return `ENOSYS` on a
0.7 kernel; the older 0.6 release also lacks required memory/runtime calls.

## Export the SDK

All userspace programs, including shell tools, have the same system-write
ceiling. Put application binaries/data in `/APPS` or `/HOME`; changes to `/RUM`
use [trusted recovery installation](system-authority.md). Neither an executable
name nor an inherited writable handle grants system-maintenance authority.

From a rum source checkout:

```powershell
.\rum.ps1 sdk -Jobs 4
```

Linux/WSL:

```sh
./rum sdk -j4
```

Both create `build/rum-sdk.zip`. Use `-Output` / `--output` to select another
ZIP path, or `-NoBuild` / `--no-build` to package existing runtime artifacts.
The normal `rum.zip` and standalone ISO remain separate. SDK export only
builds application support, not the kernel or installed system programs.

`sdk.json` records the SDK format/interface versions, `i686-elf` target,
required kernel features, source identity when available and hashes of every
payload file. Archive member order, permissions and timestamps are fixed.
Build products, user disks, upstream game sources and game assets are excluded.

## Create a project

Extract the ZIP, then copy its `template` directory into a separate project.
The SDK needs Python 3.9+, a host C compiler and the `i686-elf` cross toolchain;
Windows builds use Ubuntu/WSL through the supplied PowerShell launcher.

```sh
export RUM_SDK='/path/to/rum-sdk'
make CROSS_PREFIX=/path/to/i686-elf-
```

On Windows, run the copied project's `build.ps1`:

```powershell
.\build.ps1 -Sdk 'F:\SDKs\rum-sdk' -CrossPrefix '/mnt/f/Projects/osdev/rum/.tools/cross/bin/i686-elf-'
```

The SDK path is a Windows path; the compiler prefix is a Linux/WSL path.
The project and SDK paths may contain spaces. The build emits a stripped ELF,
debug ELF and linker map. Its host validator compiles from exported public
headers and refreshes when their contents change, including after extraction
with preserved ZIP timestamps. Application builds do not rebuild the OS.

See the [packaged SDK README](../sdk/README-sdk.md) for direct builder commands,
arguments, streams, ABI compatibility, errors, timing and runtime limitations.
The [runtime guide](application-runtime.md) and
[executable contract](executable-loading.md) describe the public behavior in
detail. [Filesystem syscalls](filesystem-syscalls.md) and
[process syscalls](process-syscalls.md) document the versioned wire structures.

## Install and test

With QEMU stopped, back up the disk and copy the stripped ELF into `/APPS`
using host FAT tools. Create the directory with `setup` in the guest first.
Use an 8.3 filename such
as `EXAMPLE.ELF`, then launch it by path. A build does not modify existing disks.

```text
run /APPS/EXAMPLE.ELF
run /APPS/EXAMPLE.ELF /SDKTEST.BIN
```

The example grows and frees its heap, handles allocation failure and checks
timing. A supplied file path is created or replaced and read back byte for
byte. `--fault` and `--spin` exercise fault and Ctrl+C cleanup.

```powershell
.\rum.ps1 test -Suite sdk -Jobs 4
```

The focused suite verifies package hashes and a build in a separate temporary
directory, then runs that external application through ELF boot at 16 MiB and
ISO boot at 64 MiB. It checks exact persistent bytes and page, region, mapping
lease and handle counts after exit, fault and cancellation. It needs no network.
The same checks are included in the normal suite. The optional
[Doom compatibility probe](doom-compatibility.md) is run separately.
