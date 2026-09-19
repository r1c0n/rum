# rum documentation

rum is a small 32-bit x86 hobby operating system. This documentation covers
building it, using the shell, and understanding the kernel well enough to make
changes without breaking its memory and interrupt-safety rules.

## Start here

1. Follow [Setup](setup.md) to install the cross-compiler, GRUB tools, and QEMU.
2. Run `./rum.ps1 run` from PowerShell or `make run` from Ubuntu.
3. Use the [Shell guide](shell.md) to explore files, diagnostics, and Snake.
4. Read [Debugging](setup.md#debugging) before changing low-level startup or
   exception code.

If you only want to try the release ISO, no compiler is required:

```sh
qemu-system-i386 -m 64M -cdrom rum.iso
```

## What works today

The normal boot provides a VGA text console, a PS/2 keyboard, a kernel shell,
temporary RAM files, diagnostics, and ASCII Snake. The kernel has physical and
virtual memory management, guarded task stacks, cooperative scheduling, and a
controlled double-fault path. Prepared processes enter ring 3 through the
production interrupt-return path, and a user exception terminates only that
process while preserving its fault record for the parent.

Separate user ELF programs also build successfully, but the normal shell does
not load or launch them yet. File changes remain in RAM and disappear on reboot.

## User guides

| Guide | Use it for |
| --- | --- |
| [Setup](setup.md) | Installing dependencies, building, running, and attaching GDB |
| [Shell](shell.md) | Commands, editing rules, RAM-file examples, and input limitations |
| [ASCII Snake](snake.md) | Controls, scoring, and score-file behavior |
| [Heap and RAM files](storage.md) | File limits, embedded files, and the kernel storage APIs |
| [Kernel diagnostics](diagnostics.md) | Reading `diag` output and investigating a panic |

## Kernel guides

| Guide | Use it for |
| --- | --- |
| [Boot, exceptions, and CPU state](exceptions.md) | GDT, TSS, IDT, interrupt frames, user CPU policy, and double faults |
| [Device interrupts](interrupts.md) | PIC routing, PIT ticks, keyboard input, and IRQ restrictions |
| [Kernel tasks and process records](tasks.md) | Scheduling, guarded stacks, events, process publication, and cleanup |
| [Memory management](memory.md) | Multiboot memory discovery, physical pages, paging, and checked user access |
| [Memory map and ownership](memory-layout.md) | Virtual ranges, limits, and who must release each resource |
| [User ABI and ELF programs](user-abi.md) | Building user code and following the public syscall, stack, and ELF contracts |

The [roadmap](roadmap.md) is project planning rather than a description of
released behavior. It lists the remaining work for protected userspace and
persistent storage.

## Boot flow

1. GRUB loads `rum.elf` and supplies the Multiboot v1 information structure.
2. `_start` establishes the boot stack, GDT, segment registers, TSS, and CPU
   policy before calling `kernel_main`.
3. The kernel starts VGA and COM1 output, installs the IDT, and configures the
   PIC, PIT, and PS/2 controller.
4. The memory manager reserves the kernel and boot data, builds paging, protects
   kernel code and constants, and leaves page zero unmapped.
5. The heap and RAM filesystem start, embedded files are copied into RAM, and
   the task system allocates guarded idle and double-fault stacks.
6. IRQ0 and a detected keyboard are unmasked. The foreground loop then handles
   shell input, game updates, and uptime while the idle task sleeps with
   `sti; hlt` whenever no work is ready.

Interrupt handlers only acknowledge hardware, update bounded state, queue
input, and signal work. Commands, drawing, memory allocation, blocking, and
resource cleanup happen in foreground task context.

## Checking a change

Run the narrowest relevant check while developing, then run the complete suite
before opening a pull request:

```sh
make test-host   # C tests for portable kernel components
make test-user   # User ELF and public-header validation
make test        # Full build plus QEMU integration cases
```

QEMU logs, screenshots, register dumps, and ownership reports are written to
`build/test-artifacts/`. These files are generated diagnostics and should not
be committed.
