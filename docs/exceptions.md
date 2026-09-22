# Boot, exceptions, and CPU state

This guide explains the x86 tables and stack frames that every interrupt,
exception, and ring-3 transition relies on. Read it before changing
files in `arch/i386/`.

## Descriptor tables

rum uses a flat 32-bit GDT:

| Selector | Purpose |
| --- | --- |
| `0x08` | Ring-0 code |
| `0x10` | Ring-0 data and stack |
| `0x1b` | Ring-3 code |
| `0x23` | Ring-3 data and stack |
| `0x28` | Normal 32-bit task-state segment |
| `0x30` | Double-fault task-state segment |

The normal TSS supplies the ring-0 stack used when the CPU enters the kernel
from ring 3. The scheduler updates its ESP0 and CR3 whenever it changes the
current context. Its I/O-map offset lies beyond the TSS limit, so ring-3 port I/O
is denied while IOPL remains zero.

The GDT is writable because `ltr` marks a TSS descriptor busy. The separate
double-fault descriptor is also changed by the hardware when its task gate is
used.

The IDT contains ring-0 interrupt gates for exceptions 0–31 and PIC vectors
`0x20`–`0x2f`, plus a ring-3 interrupt gate at `0x80` for system calls. Vector 8
is the exception: it is a hardware task gate targeting the double-fault TSS.
Unused entries are absent. Only the `0x80` gate has descriptor privilege level
3, so user code cannot invoke exception or device vectors with `int`.

## Common interrupt frame

CPU exceptions, device IRQs, and system calls enter through the same assembly
path:

1. The CPU saves EIP, CS, and EFLAGS. A privilege change also saves user ESP and
   SS. Some exceptions include a hardware error code.
2. The vector stub supplies a zero error code when the CPU did not provide one,
   then pushes the vector number.
3. Common assembly clears DF, saves general and segment registers, loads kernel
   data selectors, and aligns the stack for C.
4. `interrupt_dispatch` routes PIC vectors to the IRQ layer, vector `0x80` to
   the syscall dispatcher, and exceptions to the exception handler.
5. A returning entry restores the saved state and executes `iret`.

`struct exception_frame` is the 68-byte common prefix. A ring-3 entry uses
`struct exception_user_frame`, which adds the saved user ESP and SS for a total
of 76 bytes. Always inspect the saved CS privilege bits before reading that
tail. A ring-0 frame ends at EFLAGS.

`exception_frame_esp` and `exception_frame_ss` provide the correct values for
either frame shape. Use them instead of manually indexing the stack.

## User faults and kernel panics

An exception is recoverable only when its saved CS shows CPL 3 and the current
task is a user process. The handler records the vector, error code, EIP, ESP,
complete user frame, and CR2 for a page fault. It marks the process exited and
switches to a surviving kernel context. The parent can inspect the record before
explicitly reaping the process and its private memory.

An exception whose saved CS shows CPL 0 is fatal, even when the current task is
serving a process. The panic report includes the exception name, vector, error
code, EIP, CS, EFLAGS, general registers, segment registers, and interrupted
stack pointer. A page fault also reports CR2 and decodes whether the access was
present, writable, and from user mode.

A nested panic stops immediately to avoid recursively using corrupted state.
The final halt runs with interrupts disabled.

## Double faults

A kernel stack overflow can prevent an ordinary page-fault handler from
building its frame. Vector 8 therefore uses a hardware task switch rather than
the common interrupt gate.

The double-fault TSS selects:

- The permanent kernel page directory.
- An independent guarded 16 KiB emergency stack.
- Ring-0 code and data selectors.
- `double_fault_entry` as its entry point.

The entry reconstructs the failed EIP, registers, and ESP from the normal TSS,
prints the usual fatal report, and halts. It never returns to the failed task.
This path is intentionally small; do not allocate memory or attempt scheduling
from it.

## Trusted ring-3 frames

`cpu_user_frame_initialize` creates a clean user frame with the user selectors
and EFLAGS `0x202`. This enables maskable interrupts while keeping IOPL, DF, TF,
NT, VM, and optional flags clear. Kernel flags must never be copied into a new
user context.

Before a frame can belong to a process, its EIP must point into a mapped user
program page and its 16-byte-aligned ESP must point into mapped writable user
stack memory. `task_create_process` performs those checks and copies the trusted
frame into the process record.

`interrupt_enter` accepts a complete trusted frame and transfers it to
`interrupt_return`, which restores the frame with `iret`. The shell constructs
foreground processes from validated ELF files, and production process tasks use
this path for their first entry and every syscall or interrupt return.

rum uses an integer-only CPU policy. Kernel and user builds disable
x87, MMX, SSE, and SSE2 code generation. CR0 and CR4 are configured so actual
extended-state instructions fault because no task owns or saves that state.

## Debugging a panic

Boot the isolated invalid-opcode example with:

```powershell
./rum.ps1 panic
```

or:

```sh
make panic
```

Resolve the reported instruction address against the same ELF that produced
the panic:

```sh
.tools/cross/bin/i686-elf-addr2line -e build/rum.elf -f 0xADDRESS
```

For interactive inspection, start `make debug`, attach GDB as described in
[Setup](setup.md#debugging), and inspect `rum_gdt`, `rum_tss`,
`rum_double_fault_tss`, `idt`, and the current exception frame. The
[diagnostics guide](diagnostics.md) explains the task and ownership fields that
follow the saved registers.

## Rules for changing this code

- Keep GDT, TSS, and IDT storage mapped writable but supervisor-only.
- Preserve the exact C frame layouts asserted in `include/rum/interrupts.h`.
- Clear DF before entering C from any assembly path.
- Keep the stack aligned according to the i386 C calling convention.
- Do not read a user-frame tail after a ring-0 entry.
- Update TSS.ESP0 before code can return to a different user context.
- Switch away before releasing a faulted process's active CR3 or kernel stack.
- Keep fatal paths allocation-free and safe with IF already clear.

Run `make test-host` after changing frame definitions and `make test` after any
change to descriptors, assembly entry/return, CPU flags, or exception dispatch.

## References

- [Intel Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD System Programming Manual](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf)
- [Multiboot machine state](https://www.gnu.org/software/grub/manual/multiboot/html_node/Machine-state.html)
