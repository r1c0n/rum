# Next landfalls

Release status: `0.1.0` is unreleased. Milestones track implementation progress.
Version numbers change when we plan a release, rather than after each milestone.

1. **Complete:** boot a C kernel through GRUB with VGA and serial output.
2. **Complete:** kernel GDT, IDT, and CPU exception handlers with
   register diagnostics and real-fault tests. See [exceptions](exceptions.md).
3. **Complete:** PIC interrupt handling, a 100 Hz PIT timer with uptime,
   and PS/2 keyboard input with text echo. See [device interrupts](interrupts.md).
4. **Complete:** small command loop with `help`, `clear`, `about`, and `echo`,
   bounded line editing, and VGA/serial output. See [the shell](shell.md).
5. **Complete:** Multiboot memory map, physical page allocator, and protected
   4 KiB paging with kernel map/unmap APIs. See [memory management](memory.md).
6. **Complete:** page-backed kernel heap, mutable RAM filesystem, build-embedded
   files, and shell commands to list/read/write/remove them. See [storage](storage.md).
7. Make something fun: ASCII Snake or a tiny text adventure.

Next: milestone 7. Make something fun with the console, keyboard and RAM files:
ASCII Snake or a tiny text adventure.

Keep each milestone bootable in QEMU. Serial logs and `make test` should keep
working as the kernel grows. Add targeted tests when new hardware behavior needs them.
