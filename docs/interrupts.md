# Device interrupts (milestone 3)

rum targets QEMU's BIOS PC with legacy 8259 PICs, an 8254-compatible PIT, and
an i8042 PS/2 keyboard controller. These additions keep the unreleased version
at `0.1.0`.

## PIC and IRQ return

The master PIC is remapped to vectors `0x20`–`0x27`; the slave uses
`0x28`–`0x2f` and connects through master IRQ2. Initialization masks all sources.
The normal kernel registers handlers before unmasking IRQ0 and IRQ1, leaving
the master mask at `0xfc` and the slave at `0xff`.

Each IRQ stub builds the same ring-0 frame used for exceptions. Common assembly
clears DF, saves all general and segment registers, loads rum's data segments,
aligns the stack for C, and calls `irq_dispatch`. It then restores registers,
discards the synthetic vector/error pair, and returns with `iret`, restoring
the interrupted flags. The interrupt gate keeps IF clear during the handler;
handlers do not enable nested interrupts.

Dispatch calls the registered handler, then sends an EOI. Slave IRQs acknowledge
the slave before the master. An unregistered source is masked and acknowledged.
Spurious IRQ7 gets no EOI when master ISR bit 7 is clear. Spurious IRQ15 gets
only a master EOI when slave ISR bit 7 is clear. Fatal CPU exceptions still
print diagnostics and halt; they do not return.

## PIT and idle

PIT channel 0 uses binary mode 2 with a low-byte/high-byte divisor of 11932.
The nominal 1,193,182 Hz input gives approximately 100 interrupts per second.
IRQ0 increments an aligned, volatile 32-bit tick counter. The foreground loop
displays `ticks / TIMER_HZ` in the bottom VGA row once per second. Console
scrolling uses the other 24 rows and leaves the status row and cursor alone.
This simple uptime counter wraps after roughly 497 days; it is not a wall clock.

The loop disables interrupts while checking ticks and queued input. If work is
pending, it restores flags and processes it. Otherwise `sti; hlt` enables
interrupts and sleeps atomically through STI's interrupt shadow, so an IRQ
cannot arrive between the work check and sleep and leave an event stranded.

## PS/2 keyboard

Initialization runs with CPU interrupts disabled. It disables both controller
ports, drains stale bytes, disables controller IRQs, and enables translation.
It enables the first port, disables keyboard scanning, selects device scan code
set 2, then enables scanning again. The controller translates these bytes to
set 1 for rum's decoder. Commands require ACK (`0xfa`), retry RESEND (`0xfe`)
up to three times, and use bounded polling. The final controller configuration
enables IRQ1 and leaves the mouse port disabled. If initialization fails, IRQ1
stays masked, boot reports the failure, and the timer continues.

IRQ1 drains a bounded number of bytes, discarding mouse, parity, and timeout
data. The US QWERTY decoder handles make/break events, both Shift keys, Caps
Lock, punctuation, Enter, Tab, and Backspace. Caps Lock toggles only on its
initial make event. It recognizes E0/E1 prefixes, ignores navigation and Print
Screen's fake shifts, skips Pause's sequence, and accepts keypad Enter/slash.
Ctrl/Alt combinations are ignored. Num Lock, numeric keypad digits, cursor
navigation, keyboard LEDs, and other layouts are later work.

Printable characters enter a 128-slot ring buffer (127 usable). A full queue
drops the newest character and increments a diagnostic counter. Reading the
queue briefly saves/disables/restores interrupts. Handlers never print or edit
the console: the foreground loop performs VGA and serial echo. Backspace cannot
erase the prompt, Tab inserts four spaces, Enter starts a new prompt, and each
line accepts up to 255 characters. Command execution belongs to milestone 4.

## Verification

`./rum.ps1 test` / `make test` include host decoder and console tests, both normal
boot paths, the four CPU-fault fixtures, and a separate IRQ fixture. The fixture
uses software interrupts with known registers and DF set to check restoration
and both spurious paths. It does not pretend these are real hardware IRQs.

Normal boots test live PIT ticks, then inject keys through QMP into the emulated
PS/2 device. Checks cover Shift/Caps Lock, punctuation, release events, ignored
keys, Enter/Tab/Backspace, prompt protection, scrolling, and timer survival after
keyboard traffic. Physical memory dumps verify IRQ counts and no dropped keys;
QEMU's PIC report verifies masks. Repeated delivery exercises EOIs. Logs,
memory dumps, and screenshots are written to `build/test-artifacts/`.

References: [Intel 8259A datasheet](https://www.pcjs.org/documents/datasheets/intel/INTEL_8259A_PIC.pdf),
[Intel 8254 datasheet](https://www.cs.cmu.edu/~410/doc/8254.pdf),
[QEMU i8042 implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/pckbd.c),
[QEMU PS/2 implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/ps2.c),
and [QMP input commands](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#input).
