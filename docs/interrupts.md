# Device interrupts

rum targets the legacy PC devices provided by QEMU: two 8259 PICs, an
8254-compatible PIT, and an i8042 PS/2 keyboard controller.

## IRQ map

| IRQ | Vector | Device | Normal state |
| --- | --- | --- | --- |
| 0 | `0x20` | PIT timer | Unmasked after setup |
| 1 | `0x21` | PS/2 keyboard | Unmasked only when keyboard setup succeeds |
| 2 | `0x22` | Slave-PIC cascade | Managed by the PIC |
| 3–7 | `0x23`–`0x27` | Unused | Masked |
| 8–15 | `0x28`–`0x2f` | Unused slave IRQs | Masked |

Initialization masks every source before registering handlers. A normal boot
ends with master mask `0xfc` and slave mask `0xff`. If the keyboard is missing
or rejects configuration, rum reports the failure, leaves IRQ1 masked, and
continues with timer output and serial diagnostics.

## What an IRQ handler may do

IRQ handlers run with maskable interrupts disabled. They may:

- Read or acknowledge the device.
- Update small bounded counters or queues.
- Signal a task event and make an existing waiter runnable.

They must not allocate memory, create or switch tasks, block, reclaim resources,
run shell commands, or draw a full interface. Those operations happen after
interrupt return in foreground context. `irq_in_handler()` lets shared APIs
reject unsafe calls.

The assembly entry and return format is shared with CPU exceptions. See
[Boot, exceptions, and CPU state](exceptions.md#common-interrupt-frame) before
changing the stubs.

## PIC acknowledgement

`irq_dispatch` calls the registered handler and then sends the end-of-interrupt
command. A slave IRQ is acknowledged on the slave first and the master second.
An unexpected, unregistered source is masked before acknowledgement so it cannot
trap the kernel in an interrupt loop.

Spurious IRQ7 and IRQ15 require special handling:

- IRQ7 receives no EOI when the master in-service bit is clear.
- IRQ15 receives only a master EOI when the slave in-service bit is clear.

## Timer and idle behavior

PIT channel 0 uses mode 2 with divisor 11932, producing roughly 100 ticks per
second. IRQ0 increments a 32-bit tick counter and signals the shared foreground
work event.

The foreground loop uses ticks for the uptime row and Snake movement. It
compares unsigned tick differences, so normal scheduling continues across the
counter wrap after roughly 497 days.

When there is no input, game update, or runnable worker, the boot task waits on
the work event and the scheduler runs idle. Idle checks for work with IF clear,
then executes `sti; hlt`. The interrupt shadow after STI closes the race between
the final check and sleeping.

## Keyboard behavior

Keyboard setup disables both controller ports, drains stale bytes, enables
translation, selects keyboard scan-code set 2, and enables IRQ1. The controller
translates incoming bytes to set 1 for rum's decoder. Command polling is bounded;
ACK and RESEND are handled without waiting forever for broken hardware.

The decoder supports a US QWERTY keyboard with:

- Letters, digits, punctuation, and Space.
- Both Shift keys and Caps Lock.
- Enter, keypad Enter, Tab, and Backspace.

Navigation keys, function keys, keyboard LEDs, Num Lock behavior, and alternate
layouts are unsupported. Ctrl and Alt combinations are ignored. Pause and Print
Screen sequences are consumed without producing text.

Decoded characters enter a 128-slot ring buffer with 127 usable entries. If the
buffer is full, the newest character is dropped and the diagnostic drop counter
increases. IRQ1 never echoes or edits text; the foreground loop sends queued
characters to the [shell](shell.md) or [Snake](snake.md).

## Adding an IRQ source

1. Initialize and quiet the device while its PIC line is masked.
2. Register a short handler with `irq_register`.
3. Clear any pending device condition.
4. Unmask the PIC line only after the handler is ready.
5. Move expensive work into a foreground queue or task event.
6. Add the line to diagnostics if dropped or unexpected events matter.

Keep handler work bounded. A device that needs polling must use a finite timeout
and return an error rather than holding IF clear indefinitely.

## Troubleshooting

- **Uptime never changes:** check that IRQ0 is unmasked and that the QEMU log
  reports timer initialization.
- **Keyboard does nothing but uptime works:** keyboard setup failed or IRQ1 stayed
  masked. Read the serial boot message and inspect the PIC mask with QEMU.
- **Characters disappear during heavy input:** inspect the keyboard drop count;
  the fixed ring intentionally drops new bytes when full.
- **The CPU repeatedly enters one IRQ:** confirm the device condition is cleared
  and the correct PIC EOI sequence is used.
- **A wait occasionally hangs:** take the event sequence before checking the
  associated queue and pass that same snapshot to `task_wait`.

Run `make test` after changing PIC masks, interrupt acknowledgement, PIT setup,
keyboard command handling, queue behavior, or idle/wakeup code.

## References

- [Intel 8259A datasheet](https://www.pcjs.org/documents/datasheets/intel/INTEL_8259A_PIC.pdf)
- [Intel 8254 datasheet](https://www.cs.cmu.edu/~410/doc/8254.pdf)
- [QEMU i8042 implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/pckbd.c)
- [QEMU PS/2 implementation](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/ps2.c)
