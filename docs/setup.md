# Setup

rum builds with an `i686-elf` cross-compiler under Ubuntu 24.04 on WSL 2.
The toolchain is installed in `.tools/cross/` inside the repository. Both that
directory and `build/` are generated and excluded from Git.

## PowerShell

Install WSL 2 with an Ubuntu distribution named `Ubuntu`, and install Windows
QEMU with `qemu-system-i386.exe` or `qemu-system-x86_64.exe` on `PATH`.
From the repository directory:

```powershell
.\rum.ps1 setup
.\rum.ps1 doctor
.\rum.ps1 run
```

Setup installs Ubuntu packages using `sudo`, builds the cross-compiler, and
checks the tools. The launcher uses WSL for builds and Windows QEMU for the
interactive run, debug, and panic actions. The test action runs QEMU in WSL.
Use `-Distro <name>` if your Ubuntu distribution has a different name.

If PowerShell blocks script execution, invoke the launcher with a policy
override for that process:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\rum.ps1 run
```

## Manual setup in Ubuntu

Open an Ubuntu terminal in the repository and run:

```sh
sudo bash scripts/install-deps.sh
bash scripts/build-toolchain.sh
make doctor
make run
```

The dependency script installs the host build tools, compiler prerequisites,
GRUB BIOS modules, `xorriso`, Mtools, Python 3, QEMU, and GDB. `make doctor`
checks the compiler target, target `libgcc`, and the tools used to build and boot.

The toolchain script builds GNU Binutils 2.45 and GCC 15.2.0 for `i686-elf`,
including `libgcc`. Source archives are downloaded from GNU over HTTPS.
Compilation uses six jobs by default; reduce that if needed:

```sh
JOBS=4 bash scripts/build-toolchain.sh
```

Temporary compiler builds use `/tmp/rum-toolchain-<uid>/` on Linux storage;
diagnostics are in its `build.log`. Rerun the script to resume an interrupted
build. `RUM_TOOLCHAIN_BUILD_DIR` overrides the temporary directory.
The installed compiler runs in Ubuntu, not directly in PowerShell.

The Makefile uses the local toolchain without changing your shell profile.
To use an existing `i686-elf` toolchain on `PATH`:

```sh
make CROSS_PREFIX=i686-elf-
```

## Build commands

```sh
make                 # Build build/rum.elf and build/rum.iso
make run             # Boot the ISO through GRUB
make run-kernel      # Boot the ELF through QEMU's Multiboot loader
make test            # Run host and QEMU tests
make test-host       # Run host tests only
make clean           # Remove build/
```

QEMU starts with 64 MiB of RAM. Serial output appears in the launching terminal.
Close the QEMU window or press `Ctrl+C` in that terminal to stop it.

## Debugging

Run `.\rum.ps1 debug` or `make debug` to start QEMU paused with a GDB server.
In an Ubuntu terminal in the repository:

```sh
gdb build/rum.elf
```

```gdb
target remote localhost:1234
break kernel_main
continue
```

The PowerShell action uses Windows QEMU. If WSL cannot reach its debugger port
through localhost, use `make debug` in Ubuntu instead. C sources include debug
information, and assembly stubs have symbols.

Run `.\rum.ps1 panic` or `make panic` to boot the isolated invalid-opcode test
kernel. Use the regular run action to return to rum.

## References

- [OSDev Bare Bones](https://wiki.osdev.org/Bare_Bones)
- [GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler)
- [Multiboot v1 specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
