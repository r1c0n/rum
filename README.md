# rum

A small hobby operating system in C and x86 assembly,
named after the Scottish island of Rùm.

rum starts with the [OSDev Bare Bones tutorial](https://wiki.osdev.org/Bare_Bones):
32-bit x86, an `i686-elf` GNU toolchain, a freestanding C kernel, an ELF executable,
and GRUB using Multiboot v1. The first build targets QEMU's BIOS PC and VGA text
mode. It does not provide UEFI graphics or keyboard input yet.

## Run from PowerShell

```powershell
cd F:\Projects\osdev\rum
.\rum.ps1 doctor
.\rum.ps1 run
```

The script builds in Ubuntu on WSL and runs Windows QEMU. If your PowerShell
execution policy prevents scripts, invoke the action using:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\rum.ps1 run
```

`Bypass` applies only to that process. No permanent policy change is needed.

## Run from WSL

```sh
cd /mnt/f/Projects/osdev/rum
make doctor
make
make run
```

### Commands

| PowerShell             | WSL                 | Purpose                                                   |
| ---------------------- | ------------------- | --------------------------------------------------------- |
| `./rum.ps1 doctor`     | `make doctor`       | Check required build tools                                |
| `./rum.ps1 setup`      | See `docs/setup.md` | Install dependencies and build compiler                   |
| `./rum.ps1 build`      | `make`              | Build `build/rum.elf` and GRUB ISO `build/rum.iso`        |
| `./rum.ps1 run`        | `make run`          | Boot through GRUB                                         |
| `./rum.ps1 run-kernel` | `make run-kernel`   | Boot ELF directly through QEMU's Multiboot loader         |
| `./rum.ps1 test`       | `make test`         | Check console and memory behavior, then ISO and ELF boots |
| `./rum.ps1 debug`      | `make debug`        | Start paused with GDB server on localhost port 1234       |
| `./rum.ps1 clean`      | `make clean`        | Remove generated `build/` directory                       |

Close QEMU's window to exit. With `make run`, serial output also appears in your
terminal. `Ctrl+C` stops that foreground QEMU process.

## Source layout

```text
arch/i386/boot.s       Multiboot header, stack setup, C entry, halt loop
arch/i386/linker.ld    ELF layout; loads at 2 MiB
boot/grub/grub.cfg    GRUB boot menu
include/rum/          Kernel headers and x86 port I/O helpers
kernel/kernel.c      First greeting and Multiboot handoff check
kernel/terminal.c    VGA text, color, cursor, wrapping, newlines, scrolling
kernel/serial.c      COM1 output for debugging
kernel/memory.c      Freestanding memcpy, memmove, memset, memcmp
scripts/             Setup, environment audit, and QEMU boot tests
tests/               Host checks of console boundaries and memory routines
docs/                Setup and next milestones
Makefile             Build, validate, package, run, debug
rum.ps1              Windows commands backed by WSL
.tools/cross/        Local compiler installation (generated, ignored)
build/               Kernel, ISO, map, logs, screenshots (generated, ignored)
```

GRUB is the bootloader. `boot.s` is rum's kernel startup code: it runs after GRUB
has loaded the kernel into 32-bit protected mode. We aren't building our own
bootloader in this milestone.

The kernel keeps interrupts disabled until we have an interrupt descriptor table
and handlers. It prints a greeting and returns into `cli` / `hlt`; the machine
then idles. There are no processes, allocator, filesystem, shell, or host C library.

## Debug

Start `./rum.ps1 debug`, then open an Ubuntu terminal in the project:

```sh
gdb build/rum.elf
```

```gdb
target remote localhost:1234
break kernel_main
continue
```

This uses Windows QEMU's debugger port. If WSL's network configuration prevents
access to that port through localhost, run `make debug` inside WSL instead.

Read [setup](docs/setup.md) and [next milestones](docs/roadmap.md) for more.
