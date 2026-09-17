# Build environment

rum builds under Ubuntu 24.04 on WSL 2. The source stays on the Windows drive;
the compiler build uses Linux storage in `/tmp` for speed and filesystem semantics.
The installed cross-compiler is in the project's `.tools/cross` directory.

## Fresh setup

In Ubuntu, inside the rum project:

```sh
sudo bash scripts/install-deps.sh
bash scripts/build-toolchain.sh
make doctor
make test
```

The script builds GNU Binutils 2.45 and GCC 15.2.0 for `i686-elf`, including target
`libgcc`. These are pinned versions, not an automatic latest-version download.
Source archives come from `https://ftp.gnu.org/gnu/` over HTTPS. Compilation uses
six jobs by default; set `JOBS=4` if you need to reduce memory and CPU usage.
The toolchain is Linux executables and must run in WSL, not directly in PowerShell.

The temporary compiler build is `/tmp/rum-toolchain-1000` for UID 1000. Its
`build.log` contains build diagnostics. To resume an interrupted setup, rerun
the script. If your WSL UID differs, the temporary directory follows that UID.

No host compiler or shell profile is replaced. The Makefile addresses the local
cross-compiler explicitly. To use an existing toolchain instead:

```sh
make CROSS_PREFIX=i686-elf-
```

`make doctor` checks the compiler target, `libgcc`, GRUB's BIOS modules, Make,
`grub-file`, `grub-mkrescue`, `xorriso`, `mcopy`, Python, and QEMU.

## Boot path

1. QEMU starts a BIOS PC and boots the generated ISO.
2. GRUB reads `boot/grub/grub.cfg` and loads `rum.elf`.
3. The ELF's Multiboot header identifies rum as a Multiboot v1 kernel.
4. GRUB jumps to `_start` with its magic value in EAX and boot information in EBX.
5. Assembly creates a 16 KiB stack, clears the direction flag, aligns the stack
   for the C ABI, and calls `kernel_main(magic, info_address)`.
6. The C kernel initializes VGA and COM1, verifies the handoff, and prints.
7. The assembly halt loop idles with interrupts disabled.

The tutorial's essential build and boot approach is preserved. rum adds separate
console and serial modules, a few compiler-required memory routines, automatic
Multiboot validation, build scripts, and the terminal exercises (newlines and scrolling).

References: [Bare Bones](https://wiki.osdev.org/Bare_Bones),
[GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler),
[Multiboot v1](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html).
