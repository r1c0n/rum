# CPU exceptions

rum uses a writable GDT with flat kernel code/data at selectors `0x08`/`0x10`,
user code/data at `0x1B`/`0x23`, and a 32-bit TSS at `0x28`. Code and data
segments cover the 32-bit address space. The user descriptors prepare for
future processes; the normal kernel still runs entirely in ring 0.
Startup saves the Multiboot arguments, loads GDTR, reloads CS with a far jump,
reloads the data and stack segments, loads TR, and enters the kernel with an
aligned stack. The TSS starts with the boot stack in ESP0 and kernel data in
SS0. Its I/O-map offset is beyond the descriptor limit, denying user port I/O
when IOPL is zero. `gdt_set_kernel_stack` updates ESP0 for a caller-owned,
mapped supervisor stack. Loading TR sets the descriptor's busy bit, so the
GDT must remain writable.

`idt_initialize` installs 32-bit ring-0 interrupt gates for exceptions 0–31
and PIC IRQs 32–47 in a 256-entry IDT. Gates 48–255 are absent. Interrupt
gates clear IF on entry; CPU faults can enter with device interrupts disabled.

## Handler path

1. The CPU pushes EFLAGS, CS, EIP, and an error code for exceptions that have one.
2. The assembly stub pushes the vector and supplies a zero error code if needed.
3. Common assembly clears DF, saves general and segment registers, loads kernel
   data selectors, aligns the stack, and passes an `exception_frame` pointer to C.
4. C dispatch handles device IRQs and returns, or reports a fatal exception to
   VGA and COM1 before halting with `cli; hlt`.

All CPU exceptions are fatal, including breakpoints and NMIs. Exceptions and
device IRQs share one assembly entry/return path. `interrupt_dispatch` sends
device vectors to the IRQ handler and other vectors to the panic handler.
IRQ return restores segment registers, general registers and the CPU frame
with `iret`. A nested panic halts without printing recursively.

`exception_frame` is the 68-byte common prefix. Ring-0 entries end at EFLAGS;
ring-3 entries add the interrupted ESP and SS in a 76-byte
`exception_user_frame`. The saved CS privilege bits select the frame length.
Stack helpers read the tail only for user entries. For ring 0, interrupted ESP
comes from PUSHAD's saved ESP plus the normalized five-word CPU/stub frame.
There is no separate double-fault stack yet.

## User CPU policy

`cpu_user_frame_initialize` creates a zeroed register frame with user segment
selectors and EFLAGS `0x202`: interrupts enabled, IOPL zero, and DF, TF, NT,
VM and the other optional flags clear. The trusted caller must validate the
entry address and stack before using it. Kernel flags are never inherited.
`interrupt_return` accepts a complete trusted frame for `iret`; device IRQs
reach the same restore path after C dispatch.

The first user ABI is integer-only. Bootstrap sets CR0.MP, EM and TS, and clears
CR4.OSFXSR, OSXMMEXCPT and OSXSAVE. Actual x87/MMX/SIMD state instructions fault;
compiler options also disable floating-point and vector code generation in the
kernel. Extended register state has no owner or context-save path yet.
An unsupported instruction remains a fatal exception until process fault
recovery exists. Instructions such as fences that do not use extended register
state are not excluded by this policy.

Panic reports include the vector and name, error code, EIP, CS, EFLAGS,
general registers, segment selectors, and interrupted ESP (plus SS for a user
entry). Page faults also report CR2 and decode the access type and whether the
page was absent or protected.

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
| General protection | Load DS with selector `0x30`, beyond the GDT | `13 / 0x30` |
| Page fault | Read unmapped `0x00400000` | `14 / 0`, CR2 `0x00400000` |

The diagnostic page-fault fixture maps only the first 4 MiB. Additional test
kernels use the [kernel paging implementation](memory.md) to fault on null
access, unmapped aliases, and writes to read-only pages.

QEMU tests inspect GDT/IDT contents, TR, the hardware TSS busy bit, the kernel
entry stack and I/O-map offset. Paging tests verify GDT/TSS pages are writable
and remain supervisor-only under child CR3s. Fault tests compare known
registers and instruction addresses against panic reports, and verify the CPU
halts with IF clear. The invalid-opcode case checks that saved EFLAGS retain DF
while the handler clears it for C. Logs, memory dumps, and screenshots are in
`build/test-artifacts/`.

Five additional CPU fixtures enter ring 3 through the shared restore path and
receive at least three real PIT IRQs on a dedicated TSS kernel stack. They check
C-entry alignment, live kernel segments and flags, both stack pointers, and
register/segment/DF/IF preservation after `iret`. They then execute `ud2`,
`fldz`, MMX `pxor`, SSE `xorps`, or `outb`. Expected vectors are respectively
6, 7, 6, 6 and 13, with zero error codes at the exact instruction addresses.
The MMX test also accepts vector 7: [QEMU 8.2's decoder](https://github.com/qemu/qemu/blob/v8.2.2/target/i386/tcg/decode-new.c.inc)
checks TS before EM for MMX, giving device-unavailable priority. Both exceptions
reject the instruction before it uses extended register state.
The initial user flags must be exactly `0x202`.

These isolated fixtures leave paging disabled and replace C dispatch with an
observer; assembly entry/return, descriptors, TSS, timer and PIC acknowledgment
are the production code. They test CPU transitions without claiming process
isolation or fault recovery. The normal kernel remains in ring 0.
The host frame test puts a short kernel frame against an inaccessible page to
catch accidental reads of a user tail, and verifies fresh user-frame initialization.

## References

- [Intel Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD System Programming Manual](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf)
- [Multiboot machine state](https://www.gnu.org/software/grub/manual/multiboot/html_node/Machine-state.html)
