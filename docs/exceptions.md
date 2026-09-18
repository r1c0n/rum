# CPU exceptions

rum uses a flat ring-0 GDT: a null descriptor, code at selector `0x08`, and
data at `0x10`. Code and data segments cover the 32-bit address space.
Startup saves the Multiboot arguments, loads GDTR, reloads CS with a far jump,
reloads the data and stack segments, and enters C with an aligned stack.

`idt_initialize` installs 32-bit ring-0 interrupt gates for exceptions 0–31
and PIC IRQs 32–47 in a 256-entry IDT. Gates 48–255 are absent. Interrupt
gates clear IF on entry; CPU faults can enter with device interrupts disabled.

## Handler path

1. The CPU pushes EFLAGS, CS, EIP, and an error code for exceptions that have one.
2. The assembly stub pushes the vector and supplies a zero error code if needed.
3. Common assembly clears DF, saves general and segment registers, loads kernel
   data selectors, aligns the stack, and passes an `exception_frame` pointer to C.
4. The panic handler prints to VGA and COM1, then halts with `cli; hlt`.

All CPU exceptions are fatal, including breakpoints and NMIs. The frame handles
same-privilege ring-0 exceptions on the current kernel stack; there is no
separate double-fault stack. A nested panic halts without printing recursively.
Device IRQs use a separate path that returns with `iret`.

Panic reports include the vector and name, error code, EIP, CS, EFLAGS,
general registers, segment selectors, and interrupted ESP. Page faults also
report CR2 and decode the access type and whether the page was absent or protected.

## Inspecting a panic

Run `.\rum.ps1 panic` or `make panic` to boot `build/tests/fault-ud.elf`, an
isolated kernel that executes `ud2`. Use the normal run command to boot rum again.

To resolve an instruction address from a rum panic, run in Ubuntu:

```sh
.tools/cross/bin/i686-elf-addr2line -e build/rum.elf -f 0xADDRESS
```

C sources include debug information. GDB can inspect assembly stubs and symbols
such as `rum_gdt`, `idt`, `exception_dispatch`, and `cpu_halt`.
See [setup](setup.md#debugging) for attaching GDB.

## Tests

The test kernels include the kernel startup, GDT, IDT, and panic handler.
Fault fixtures keep device interrupts disabled.

| Fault | Trigger | Vector / error code |
| --- | --- | --- |
| Divide error | Divide by zero | `0 / 0` |
| Invalid opcode | `ud2` with DF set | `6 / 0` |
| General protection | Load DS with selector `0x18`, beyond the GDT | `13 / 0x18` |
| Page fault | Read unmapped `0x00400000` | `14 / 0`, CR2 `0x00400000` |

The diagnostic page-fault fixture maps only the first 4 MiB. Additional test
kernels use the [kernel paging implementation](memory.md) to fault on null
access, unmapped aliases, and writes to read-only pages.

QEMU tests inspect GDT/IDT contents and segment selectors, compare known
registers and instruction addresses against panic reports, and verify the CPU
halts with IF clear. The invalid-opcode case checks that saved EFLAGS retain DF
while the handler clears it for C. Logs, memory dumps, and screenshots are in
`build/test-artifacts/`.

## References

- [Intel Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD System Programming Manual](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf)
- [Multiboot machine state](https://www.gnu.org/software/grub/manual/multiboot/html_node/Machine-state.html)
