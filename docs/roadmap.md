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
5. Use Multiboot's memory map to build a physical page allocator and paging.
6. Add a heap, then a small RAM filesystem and embedded files.
7. Make something fun: ASCII Snake or a tiny text adventure.

Next: milestone 5. Use Multiboot's memory map to build a physical page allocator
and paging.

Keep each milestone bootable in QEMU. Serial logs and `make test` should keep
working as the kernel grows. Add targeted tests when new hardware behavior needs them.
