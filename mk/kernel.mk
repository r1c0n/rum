KERNEL_RULES := Makefile mk/config.mk mk/kernel.mk

CORE_SOURCES := kernel/core/init.c kernel/core/memory.c
DRIVER_SOURCES := drivers/display/terminal.c drivers/serial/serial.c \
                  drivers/input/keyboard.c \
                  drivers/input/keyboard_decode.c drivers/storage/ata.c
MM_SOURCES := kernel/mm/pmm.c kernel/mm/heap.c kernel/mm/vm.c
OBJECT_SOURCES := kernel/object/object.c kernel/object/handles.c
PROCESS_SOURCES := kernel/process/task.c kernel/process/session.c kernel/process/jobs.c kernel/process/syscall.c kernel/process/handles.c \
                   kernel/process/elf.c kernel/process/process.c kernel/process/reference.c
FS_SOURCES := kernel/fs/path.c kernel/fs/fs-types.c \
              kernel/fs/fs.c kernel/fs/fat16.c kernel/fs/fat16-write.c
IO_SOURCES := kernel/io/block.c kernel/io/pipe.c kernel/io/terminal.c
HAL_SOURCES := hal/pc/pic.c hal/pc/irq.c hal/pc/pit.c
RECOVERY_SOURCES := kernel/recovery/shell.c kernel/recovery/snake.c apps/snake/model.c
DEBUG_SOURCES := kernel/debug/diagnostics.c kernel/debug/diagnostics-report.c
ARCH_SOURCES := arch/i386/cpu.c arch/i386/gdt.c arch/i386/interrupt.c \
                arch/i386/exceptions.c \
                arch/i386/paging.c
SOURCES := $(CORE_SOURCES) $(DRIVER_SOURCES) $(MM_SOURCES) $(OBJECT_SOURCES) \
           $(PROCESS_SOURCES) $(FS_SOURCES) $(IO_SOURCES) \
           $(HAL_SOURCES) $(RECOVERY_SOURCES) \
           $(DEBUG_SOURCES) $(ARCH_SOURCES)
ASM_SOURCES := arch/i386/boot.s arch/i386/interrupts.s arch/i386/context.s
OBJECTS := $(ASM_SOURCES:%.s=build/%.o) build/arch/i386/gdt-load.o $(SOURCES:%.c=build/%.o)
DEPENDENCIES := $(SOURCES:%.c=build/%.d) build/arch/i386/boot.d build/arch/i386/interrupts.d build/arch/i386/gdt-load.d
build/arch/i386/gdt.o: arch/i386/gdt.c $(KERNEL_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/arch/i386/gdt-load.o: arch/i386/gdt.s kernel/include/rum/cpu_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/arch/i386/interrupts.o: arch/i386/interrupts.s kernel/include/rum/cpu_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

# The boot assembly and linker script share the memory layout constants.
build/arch/i386/boot.o: arch/i386/boot.s kernel/include/rum/memory_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

$(LINKER_SCRIPT): arch/i386/linker.ld kernel/include/rum/memory_layout.h $(KERNEL_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -E -P -undef -x c -D__ASSEMBLER__ $< -o $@

build/%.o: %.s
	@mkdir -p $(@D)
	$(AS) $< -o $@

build/%.o: %.c $(KERNEL_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/rum.elf: $(OBJECTS) $(LINKER_SCRIPT)
	$(CC) $(LDFLAGS) $(OBJECTS) -lgcc -o $@

check: build/rum.elf
	grub-file --is-x86-multiboot $<
	@echo "rum: Multiboot v1 header verified."

build/rum.iso: build/rum.elf boot/grub/grub.cfg | check
	@mkdir -p build/isodir/boot/grub
	cp build/rum.elf build/isodir/boot/rum.elf
	rm -f -- build/isodir/boot/rum-system.img
	cp boot/grub/grub.cfg build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/isodir

iso: build/rum.iso

