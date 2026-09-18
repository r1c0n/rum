# rum

A small hobby operating system in C and x86 assembly,
named after the Scottish island of Rùm.

rum starts with the [OSDev Bare Bones tutorial](https://wiki.osdev.org/Bare_Bones):
32-bit x86, an `i686-elf` GNU toolchain, a freestanding C kernel, an ELF executable,
and GRUB using Multiboot v1. The first build targets QEMU's BIOS PC and VGA text
mode. It now handles PIC interrupts, a PIT timer, PS/2 keyboard input, and a
small command loop, physical page allocation, protected paging, a kernel heap,
and a mutable RAM filesystem with embedded boot files. It also plays ASCII Snake
using the console, PS/2 keyboard, PIT timer, heap and RAM score file.
Version `0.1.0` remains unreleased; roadmap progress does not bump the version.

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
| `./rum.ps1 test`       | `make test`         | Check console/memory/heap/files/keyboard/shell, boots, faults, and IRQs |
| `./rum.ps1 debug`      | `make debug`        | Start paused with GDB server on localhost port 1234       |
| `./rum.ps1 panic`      | `make panic`        | Show a deliberate invalid-opcode panic in a separate test kernel |
| `./rum.ps1 clean`      | `make clean`        | Remove generated `build/` directory                       |

Close QEMU's window to exit. With `make run`, serial output also appears in your
terminal. `Ctrl+C` stops that foreground QEMU process.

Click inside QEMU and type at the `> ` prompt. Enter runs the command;
Backspace edits, Tab inserts four spaces, and Shift/Caps Lock work with a US
QWERTY layout. The bottom row shows uptime from timer interrupts. QEMU releases captured
keyboard/mouse input with `Ctrl+Alt+G` in its default GTK/SDL interface.

| Command | Purpose |
| --- | --- |
| `help` | List available commands |
| `clear` | Clear the console and return to the top; preserve uptime |
| `about` | Show rum's version and kernel information |
| `echo <text>` | Print text, for example `echo hello from rum` |
| `ls` | List RAM files and sizes |
| `cat <name>` | Read a file, for example `cat welcome.txt` |
| `write <name> [text]` | Create/replace a file, for example `write notes.txt hello` |
| `rm <name>` | Remove a file |
| `mem` | Show heap and RAM file usage |
| `snake` | Play ASCII Snake with WASD; P pauses, R restarts, Q quits |

Commands are lowercase. Empty lines return a prompt, and unknown commands show
a message. See [the command loop](docs/shell.md) for input rules and limits.
Files start from `assets/ramfs/` and changes are temporary. Add assets and rebuild
to embed more boot files. See [heap and RAM files](docs/storage.md).
Type `snake` to play on a 40x16 board. Eat `*`, avoid the walls and your body,
and use Q to return to the shell. Best scores are temporary and saved in
`snake.score` for the current boot. See [Snake](docs/snake.md).

## Source layout

```text
arch/i386/boot.s       Multiboot header, stack setup, C entry, halt loop
arch/i386/gdt.s        Flat kernel GDT, segment reload, shared CPU halt routine
arch/i386/interrupts.s CPU exception and returning IRQ stubs
arch/i386/exceptions.c IDT setup and VGA/serial panic diagnostics
arch/i386/pic.c        Cascaded 8259 PIC remapping, masks, EOIs, spurious IRQs
arch/i386/irq.c        IRQ dispatch and device handler registration
arch/i386/paging.c     Page tables, identity mappings, protection, map/unmap
arch/i386/linker.ld    ELF layout; loads at 2 MiB
boot/grub/grub.cfg    GRUB boot menu
include/rum/          Kernel headers and x86 port I/O helpers
kernel/kernel.c      Boot checks, keyboard dispatch, uptime, interrupt-safe idle
kernel/shell.c       Line editing, built-ins, RAM file commands and usage reports
kernel/snake_model.c Pure Snake rules, body ring, food, turns and collisions
kernel/snake.c       Timed game, positioned ASCII display, controls and RAM score
kernel/terminal.c    VGA text, cursor, scrolling, reserved status row
kernel/timer.c       PIT channel 0 periodic timer on IRQ0
kernel/keyboard.c    PS/2 controller initialization and IRQ1 character queue
kernel/keyboard_decode.c  US scan code decoding and modifier state
kernel/serial.c      COM1 output for debugging
kernel/memory.c      Freestanding memcpy, memmove, memset, memcmp
kernel/pmm.c         Multiboot RAM map, boot reservations, 4 KiB page allocator
kernel/heap.c        Page-backed allocation, split/coalesce, calloc/realloc
kernel/ramfs.c       Mutable flat RAM filesystem with copied binary payloads
assets/ramfs/        Files embedded at build time and copied into RAM at boot
scripts/             Setup, environment audit, and QEMU boot tests
tests/               Host checks, CPU/IRQ/paging fixtures, allocation and protection tests
docs/                Setup and next milestones
Makefile             Build, validate, package, run, debug
rum.ps1              Windows commands backed by WSL
.tools/cross/        Local compiler installation (generated, ignored)
build/               Kernel, ISO, map, logs, screenshots (generated, ignored)
```

GRUB is the bootloader. `boot.s` is rum's kernel startup code: it runs after GRUB
has loaded the kernel into 32-bit protected mode. We aren't building our own
bootloader in this milestone.

rum loads its own GDT before entering C and installs IDT gates for CPU
exception vectors 0–31. Fatal exceptions show their name, error code, EIP, EFLAGS,
and registers on VGA and COM1, then halt. Page faults also show CR2 and basic
access information. PIC IRQs use vectors 32–47 with a separate `iret` return
path. Only IRQ0 (timer) and IRQ1 (keyboard) are unmasked. Normal boot enters an
interrupt-driven command loop with a status row for uptime.
The physical allocator uses Multiboot's memory map, reserves the kernel and boot
data, and returns individual 4 KiB RAM pages below 1 GiB. Paging keeps an identity
window for physical access, protects kernel code/constants, and leaves page zero
unmapped. Kernel mapping APIs support additional aliases above 1 GiB. There are
no processes or host C library. The heap grows into a reserved 4 MiB virtual
window above 1 GiB; allocations are aligned to 16 bytes and freed blocks are
reused/coalesced. RAM files support list/read/create/replace/remove, with up to
64 files of 64 KiB each. They are restored from embedded assets on reboot.
See [memory management](docs/memory.md) and [heap and RAM files](docs/storage.md).

`./rum.ps1 panic` uses a separate test ELF that deliberately executes `ud2`.
Close its QEMU window and use `./rum.ps1 run` for the normal rum build.
`make test` checks normal ISO/ELF boots and real divide, invalid-opcode, protection,
and page faults. It verifies CPU tables, selectors, registers, error codes,
instruction addresses, stack pointers, direction flags, VGA/serial output, and halt.
The original page-fault test keeps its own small paging fixture for register
diagnostics. Separate fixtures exercise production paging and real faults for
null access, unmapped aliases, and writes to read-only code/constants/pages.
The suite checks memory-map reservations, page-table contents, allocation and
freeing, translation changes, and initialization/runtime allocation failures.
Boot checks cover 16, 64, 256, and 1152 MiB of RAM, including the 1 GiB cap.
The suite also checks IRQ register/flag restoration, spurious
IRQ7/IRQ15, live PIT ticks, PIC masks, and keyboard echo/editing/modifiers/scrolling
through QEMU's emulated PS/2 device, followed by shell/file commands, usage errors,
empty input, and recovery after `clear`. See [exception handling](docs/exceptions.md),
[device interrupts](docs/interrupts.md), and [the command loop](docs/shell.md).
Heap tests exercise real allocation, fragmentation, resizing, limits and physical
OOM rollback at 16 and 64 MiB. File tests include exact binary bytes, empty/max-size
files, failed atomic writes, and build embedding. Normal boots inspect the live
heap mappings/headers/file nodes and compare every embedded byte to the assets.
Snake tests cover game rules, full-board victory, pause/restart/quit, wraparound
timing, failure recovery and allocation cleanup. QEMU tests steer to food using
real PS/2 keys, verify PIT-driven movement, save scores and return to the shell.

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
