# Documentation

| Guide | Contents |
| --- | --- |
| [Setup](setup.md) | Dependencies, cross-compiler, build commands, and GDB |
| [Shell](shell.md) | Commands, line editing, and input rules |
| [Snake](snake.md) | Controls, scoring, and game implementation |
| [CPU exceptions](exceptions.md) | GDT/TSS, shared interrupt frames, user CPU policy, and panic diagnostics |
| [Device interrupts](interrupts.md) | PIC, PIT, keyboard driver, and idle loop |
| [Kernel tasks](tasks.md) | Cooperative contexts, private stacks, event waits, and deferred cleanup |
| [Kernel diagnostics](diagnostics.md) | Task, CR3, stack and resource snapshots, allocation boundaries and owned-context panics |
| [User ABI and executables](user-abi.md) | Shared syscall/argument contracts, static ELF32 builds, and startup tests |
| [Memory management](memory.md) | Physical pages, paging contexts, and shared kernel mappings |
| [Memory layout and ownership](memory-layout.md) | Shared ranges, stack reservations, process limits, and cleanup rules |
| [Heap and RAM files](storage.md) | Allocation APIs, file ownership, and embedded assets |
| [0.2.0 roadmap](roadmap.md) | Kernel prerequisites, user processes, syscalls, and persistent storage |

## Boot sequence

1. GRUB loads `rum.elf` from the ISO using Multiboot v1 and passes boot
   information to `_start`.
2. Assembly sets up a 16 KiB stack, clears the direction flag, loads the kernel
   GDT and segment selectors, loads the TSS, applies the integer-only CPU policy,
   and calls `kernel_main`.
3. The kernel initializes VGA and COM1, installs the IDT, configures the PIC,
   PIT, and PS/2 keyboard, and validates the Multiboot handoff.
4. The physical allocator reserves occupied memory. Paging creates the identity
   window, protects kernel code and constants, and leaves page zero unmapped.
5. The heap maps its first page, and the filesystem copies embedded files into RAM.
6. The task system registers boot and allocates the private idle stack.
7. The kernel unmasks the timer and available keyboard IRQs, enables interrupts,
   and enters the foreground loop.

The foreground loop handles queued keyboard input, shell commands, Snake
movement, and uptime. It waits on timer/keyboard events when no work is pending;
the idle context sleeps with `sti; hlt`. IRQ handlers update
counters or queue input; they do not run commands, allocate memory, or draw.

## Tests

Run `make test` in Ubuntu or `.\rum.ps1 test` in PowerShell. Host tests exercise
the kernel's console, memory routines and layout, keyboard decoder, shell, allocator,
heap, filesystem, and Snake rules. QEMU tests check both GRUB and direct ELF
boots, live device input, isolated CPU-fault and allocation-failure cases, and
ring-3 timer returns and CPU-policy enforcement in dedicated test kernels.
Kernel-task fixtures check actual stack/CR3 switching, wait boundaries and
repeated cleanup at 16 and 64 MiB RAM.
Diagnostics fixtures cover exact and fragmented allocation budgets and a kernel
panic on an owned worker stack under a private CR3, with independent PMM checks.
User fixtures execute separate ELF32 assets to check startup, arguments, BSS,
syscall wrappers and return values through a test-only dispatcher.

Test artifacts are written to `build/test-artifacts/`. The implementation guides
describe the checks relevant to each subsystem.
