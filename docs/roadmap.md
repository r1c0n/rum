# Next landfalls

1. Boot a C kernel through GRUB with VGA and serial output (first milestone).
2. Set up our own GDT, an IDT, and CPU exception handlers; make faults debuggable.
3. Add PIC interrupt handling, a PIT timer, and PS/2 keyboard input.
4. Build a small command loop: `help`, `clear`, `about`, and `echo`.
5. Use Multiboot's memory map to build a physical page allocator and paging.
6. Add a heap, then a small RAM filesystem and embedded files.
7. Make something fun: ASCII Snake or a tiny text adventure.

Keep each milestone bootable in QEMU. Serial logs and `make test` should keep
working as the kernel grows. Add targeted tests when new hardware behavior needs them.
