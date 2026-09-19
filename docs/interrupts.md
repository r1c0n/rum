# Device interrupts

rum targets QEMU's BIOS PC with legacy 8259 PICs, an 8254-compatible PIT,
and an i8042 PS/2 keyboard controller.

## PIC and IRQ return

The master PIC uses vectors `0x20`–`0x27`; the slave uses `0x28`–`0x2f`
and connects through master IRQ2. Initialization masks all sources. The kernel
registers handlers before unmasking IRQ0 and IRQ1, leaving the master mask at
`0xfc` and the slave at `0xff`. IRQ1 stays masked if keyboard setup fails.

IRQ and exception stubs share the same [frame prefix](exceptions.md#handler-path).
Ring-3 entries also carry the CPU-saved user ESP and SS. Common assembly
clears DF, saves general and segment registers, loads kernel data selectors,
aligns the stack, and calls `interrupt_dispatch`, which routes device vectors
to `irq_dispatch`. It restores the frame and returns
with `iret`, including the interrupted flags. Handlers run with IF clear.

Dispatch calls the registered handler and sends an end-of-interrupt command
(EOI). Slave IRQs acknowledge the slave before the master. An unregistered
source is masked and acknowledged. Spurious IRQ7 gets no EOI when master ISR
bit 7 is clear; spurious IRQ15 gets only a master EOI when slave ISR bit 7 is
clear.

## Timer and idle loop

PIT channel 0 uses binary mode 2 with a divisor of 11932. Its nominal
1,193,182 Hz input produces approximately 100 interrupts per second.
IRQ0 increments an aligned, volatile 32-bit tick counter and signals the
foreground work event. Accepted keyboard characters signal the same event.

The foreground loop displays `ticks / TIMER_HZ` in the bottom VGA row once
per second and uses ticks to advance Snake. Scrolling is limited to the other
24 rows. The uptime counter wraps after roughly 497 days.

The loop snapshots the work-event sequence while checking ticks and queued
input with interrupts disabled. It restores flags before processing or waiting.
`task_wait` checks the snapshot and attaches the blocked context atomically,
so an event between checking and waiting cannot be lost. The scheduler selects
the private idle context when nothing is runnable. Idle checks runnable state
with IF clear and sleeps with `sti; hlt`, using STI's interrupt shadow to close
the sleep boundary. See [Kernel tasks](tasks.md).

IRQ handlers may signal events and make blocked tasks runnable. They never
switch stacks, allocate tasks, block or reclaim exited contexts. Actual
scheduling happens after IRQ return; `irq_in_handler` enforces those task-API
restrictions.

## PS/2 keyboard

Setup runs with interrupts disabled. It disables both controller ports, drains
stale bytes, enables translation, and configures the keyboard to use scan code
set 2. The controller translates input to set 1 for rum's decoder. Commands
require ACK (`0xfa`), retry RESEND (`0xfe`) up to three times, and use bounded
polling. The final configuration enables IRQ1 and leaves the mouse port disabled.
If setup fails, boot reports it and the timer continues running.

IRQ1 drains a bounded number of bytes and discards mouse, parity, and timeout
data. The US QWERTY decoder handles make/break events, both Shift keys,
Caps Lock, punctuation, Enter, Tab, and Backspace. Caps Lock toggles on the
initial make event. The decoder handles E0/E1 prefixes, skips Pause and Print
Screen sequences, and accepts keypad Enter and slash.

Ctrl/Alt combinations and navigation keys are ignored. Other layouts, keyboard
LEDs, Num Lock, and numeric keypad digits are unsupported.

Characters enter a 128-slot ring buffer with 127 usable slots. A full queue
drops the newest character and increments a diagnostic counter. Reading it
briefly saves, disables, and restores interrupt flags. The foreground loop
passes characters to the [shell](shell.md) or active [game](snake.md).
IRQ handlers do not echo, edit, or draw.

## Tests

Host tests cover decoding, modifiers, and console behavior. An isolated IRQ
kernel uses software interrupts with known registers and DF set to check frame
restoration and spurious IRQ handling.
User CPU fixtures also check repeated real timer delivery through a TSS stack
switch and `iret` back to ring 3, including segments, stack, flags and C alignment.

Normal QEMU boots check live PIT ticks and inject input into the emulated PS/2
device. Tests cover modifiers, ignored keys, editing, scrolling, timer delivery,
IRQ counts, queue drops, and PIC masks. Repeated delivery exercises EOIs.
Artifacts are saved in `build/test-artifacts/`.

## References

- [Intel 8259A datasheet](https://www.pcjs.org/documents/datasheets/intel/INTEL_8259A_PIC.pdf)
- [Intel 8254 datasheet](https://www.cs.cmu.edu/~410/doc/8254.pdf)
- [QEMU i8042 implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/pckbd.c)
- [QEMU PS/2 implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/ps2.c)
- [QMP input commands](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#input)
