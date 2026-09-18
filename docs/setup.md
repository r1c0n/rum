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
   for the C ABI, saves the Multiboot arguments, loads rum's own GDT and segment
   selectors, and calls `kernel_main(magic, info_address)`.
6. The C kernel initializes VGA and COM1, installs the IDT, remaps and masks the
   PIC, programs the PIT, initializes the PS/2 keyboard, and verifies the handoff.
   CPU exceptions enter the panic handler.
7. The kernel validates the Multiboot memory map, reserves occupied pages,
   allocates a page directory/tables, and enables paging with write protection.
   The existing code, stack, VGA, GDT, and IDT retain identity addresses.
8. The kernel maps its initial heap page, initializes the RAM filesystem, and
   copies build-embedded assets into mutable heap allocations.
9. After printing boot checks, rum unmasks IRQ0 and (if initialization succeeded)
   IRQ1, then enables CPU interrupts. IRQs return after acknowledging the PIC.
10. The foreground loop feeds queued keys into the shell, runs completed command
   lines, and refreshes uptime. It sleeps with
   `sti; hlt` when no work is pending; hardware interrupts wake it again.

The tutorial's essential build and boot approach is preserved. rum adds separate
console and serial modules, a few compiler-required memory routines, automatic
Multiboot validation, build scripts, and the terminal exercises (newlines and scrolling).

Milestone 2 adds a kernel GDT, IDT, and exception diagnostics. The implementation
and fault tests are described in [exceptions.md](exceptions.md).
Milestone 3 adds the live timer and keyboard paths described in
[interrupts.md](interrupts.md).
Milestone 4 adds the command loop described in [shell.md](shell.md).
Milestone 5 adds the memory map, page allocator, and paging described in
[memory.md](memory.md).
Milestone 6 adds the page-backed heap, RAM filesystem and embedded files described
in [storage.md](storage.md).

References: [Bare Bones](https://wiki.osdev.org/Bare_Bones),
[GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler),
[Multiboot v1](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html).
