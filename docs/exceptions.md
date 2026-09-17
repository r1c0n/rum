# CPU exceptions (milestone 2)

rum owns a flat ring-0 GDT: null descriptor, code at selector `0x08`, and data
at `0x10`. The code/data segments cover the 32-bit address space. Startup saves
the Multiboot arguments, loads GDTR, reloads CS with a far jump, reloads the
data/stack segments, then enters C with a 16-byte-aligned call stack.

`idt_initialize` installs present, ring-0, 32-bit interrupt gates for exceptions
0–31 and PIC IRQs 32–47 in a 256-entry IDT. Gates 48–255 are absent. Interrupt
gates clear IF on entry; CPU faults can enter even when IF was already clear.
Normal rum now enables the timer and keyboard; the fault fixtures keep IF clear.

## Handler path

1. The CPU pushes EFLAGS, CS, EIP, and an error code for exceptions that have one.
2. The assembly stub pushes the vector and supplies a zero error code when needed.
3. Common assembly clears DF, saves general and segment registers, loads kernel
   data selectors, aligns the stack, and passes an `exception_frame` pointer to C.
4. The panic handler prints to VGA and COM1, then calls `cpu_halt` (`cli` / `hlt`).

All exceptions are fatal for now, including breakpoints and NMIs. There is no
exception recovery path or user mode. Device IRQs have their own `iret` path.
The frame supports same-privilege ring-0
exceptions and uses the current kernel stack. A separate double-fault stack
belongs to later work. A nested panic halts without recursively printing.

The panic includes vector/name, error, EIP, CS, EFLAGS, general registers, segment
selectors, and the interrupted ESP. Page faults also include CR2 and whether the
fault was a missing page or a protection violation, read/write, and user/supervisor.

## Try it

```powershell
cd F:\Projects\osdev\rum
.\rum.ps1 panic
```

This boots `build/tests/fault-ud.elf`, a separate test kernel. Normal `rum.elf`
and `rum.iso` keep their normal command loop and timer. `make panic` does the same
from WSL. Each fault ELF includes production startup, GDT, IDT, and panic code.

## Verification

`make test` / `./rum.ps1 test` check normal GRUB ISO and direct ELF boots, plus:

| Fault | Trigger | Expected vector/error |
| --- | --- | --- |
| Divide error | Divide by zero | `0 / 0` |
| Invalid opcode | `ud2`, with DF set | `6 / 0` |
| General protection | Load DS with selector `0x18`, beyond the GDT | `13 / 0x18` |
| Page fault | Supervisor read at unmapped `0x00400000` | `14 / 0`, CR2 `0x00400000` |

The page-fault fixture maps only the first 4 MiB in a separate kernel. Production
paging remains a later milestone. The tests use actual faulting instructions;
software `int` instructions do not reproduce CPU-supplied exception error codes.

QMP checks GDTR/IDTR bases and limits, segment selectors, GDT bytes, all 32 IDT
exception gates and 16 PIC gates. Fault fixtures have disabled device interrupts.
Known register values, ELF instruction
symbols, and a saved ESP are compared with the VGA and serial panic reports.
The invalid-opcode test checks that the saved EFLAGS retain DF while the handler
clears DF for C. Finally, the CPU must be in `cpu_halt` with IF clear.
Logs, memory dumps, and screenshots are in `build/test-artifacts/`.

To resolve an instruction address from a normal rum panic, run in WSL:

```sh
.tools/cross/bin/i686-elf-addr2line -e build/rum.elf -f 0xADDRESS
```

C files contain debug information. GDB can also inspect the named assembly
stubs, `rum_gdt`, `idt`, `exception_dispatch`, and `cpu_halt`.

Architecture references: [Intel Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
[AMD System Programming Manual](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf),
and [Multiboot machine state](https://www.gnu.org/software/grub/manual/multiboot/html_node/Machine-state.html).
