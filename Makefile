.DEFAULT_GOAL := all

CROSS_PREFIX ?= $(CURDIR)/.tools/cross/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
AS := $(CROSS_PREFIX)as
QEMU ?= qemu-system-i386
HOST_CC ?= gcc

CPPFLAGS := -Iinclude
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Werror \
          -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
          -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -msoft-float -mno-mmx -mno-sse -mno-sse2
LAYOUT_HEADERS := include/rum/memory_layout.h include/rum/process_limits.h
LINKER_SCRIPT := build/arch/i386/linker.ld
LDFLAGS := -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie \
           -Wl,--build-id=none -Wl,-Map,build/rum.map
SOURCES := kernel/kernel.c kernel/terminal.c kernel/serial.c kernel/memory.c kernel/timer.c kernel/keyboard.c kernel/keyboard_decode.c kernel/shell.c kernel/snake.c kernel/snake_model.c kernel/pmm.c kernel/task.c kernel/heap.c kernel/ramfs.c arch/i386/cpu.c arch/i386/gdt.c arch/i386/interrupt.c arch/i386/exceptions.c arch/i386/pic.c arch/i386/irq.c arch/i386/paging.c
ASM_SOURCES := arch/i386/boot.s arch/i386/interrupts.s arch/i386/context.s
OBJECTS := $(ASM_SOURCES:%.s=build/%.o) build/arch/i386/gdt-load.o $(SOURCES:%.c=build/%.o) build/generated/embedded-files.o
DEPENDENCIES := $(SOURCES:%.c=build/%.d) build/arch/i386/boot.d build/arch/i386/interrupts.d build/arch/i386/gdt-load.d build/generated/embedded-files.d
FAULT_KERNELS := build/tests/fault-de.elf build/tests/fault-ud.elf build/tests/fault-gp.elf build/tests/fault-pf.elf
FAULT_OBJECTS := $(FAULT_KERNELS:.elf=.o)
FAULT_COMMON := $(filter-out build/kernel/kernel.o,$(OBJECTS)) build/tests/fault-trigger.o
PAGING_CASES := ok null text rodata unmapped readonly
PAGING_KERNELS := $(addprefix build/tests/paging-,$(addsuffix .elf,$(PAGING_CASES)))
PAGING_OBJECTS := $(PAGING_KERNELS:.elf=.o)
CPU_CASES := irq x87 mmx sse io
CPU_KERNELS := $(addprefix build/tests/cpu-,$(addsuffix .elf,$(CPU_CASES)))
CPU_OBJECTS := $(CPU_KERNELS:.elf=.o)
TEST_DEPENDENCIES := $(FAULT_OBJECTS:.o=.d) $(PAGING_OBJECTS:.o=.d) $(CPU_OBJECTS:.o=.d) build/tests/cpu-probe.d build/tests/paging-spaces.d build/tests/irq-kernel.d build/tests/storage-kernel.d build/tests/storage-checks.d build/tests/task-kernel.d
STORAGE_HOST_SOURCES := kernel/heap.c kernel/ramfs.c kernel/memory.c tests/page-backend.c build/embedded-files.c
STORAGE_HOST_HEADERS := include/rum/heap.h include/rum/ramfs.h include/rum/embedded.h include/rum/paging.h include/rum/pmm.h include/rum/memory.h tests/page-backend.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
SNAKE_HOST_SOURCES := kernel/snake.c kernel/snake_model.c
SNAKE_HOST_HEADERS := include/rum/snake.h include/rum/snake_model.h include/rum/timer.h

.PHONY: all check iso run run-kernel debug panic test test-host doctor toolchain clean FORCE
.SECONDARY: $(FAULT_OBJECTS) $(PAGING_OBJECTS) $(CPU_OBJECTS) build/tests/paging-spaces.o
all: iso

build/arch/i386/gdt.o: arch/i386/gdt.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/arch/i386/gdt-load.o: arch/i386/gdt.s include/rum/cpu_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/arch/i386/interrupts.o: arch/i386/interrupts.s include/rum/cpu_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

# The boot assembly and linker script share the memory layout constants.
build/arch/i386/boot.o: arch/i386/boot.s include/rum/memory_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

$(LINKER_SCRIPT): arch/i386/linker.ld include/rum/memory_layout.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -E -P -undef -x c -D__ASSEMBLER__ $< -o $@

build/%.o: %.s
	@mkdir -p $(@D)
	$(AS) $< -o $@

build/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Check the directory each build, including removed assets. Unchanged C keeps mtime.
FORCE:
build/embedded-files.c: FORCE scripts/embed-files.py $(wildcard assets/ramfs/*)
	python3 scripts/embed-files.py assets/ramfs $@

build/generated/embedded-files.o: build/embedded-files.c Makefile
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
	cp boot/grub/grub.cfg build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/isodir

iso: build/rum.iso

run: iso
	$(QEMU) -m 64M -boot d -cdrom build/rum.iso -serial stdio -no-reboot -no-shutdown

run-kernel: check
	$(QEMU) -m 64M -kernel build/rum.elf -serial stdio -no-reboot -no-shutdown

debug: iso
	$(QEMU) -m 64M -boot d -cdrom build/rum.iso -serial stdio -no-reboot -no-shutdown -S -s

panic: build/tests/fault-ud.elf
	$(QEMU) -m 64M -kernel $< -serial stdio -no-reboot -no-shutdown

test: test-host iso $(FAULT_KERNELS) build/tests/irq.elf $(CPU_KERNELS) $(PAGING_KERNELS) build/tests/storage.elf build/tests/task.elf
	python3 scripts/smoke-test.py --qemu $(QEMU)

build/tests/irq.elf: build/tests/irq-kernel.o build/tests/irq-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/storage.elf: build/tests/storage-kernel.o build/tests/storage-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/task.elf: build/tests/task-kernel.o build/tests/task-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/cpu-irq.o: CPU_CASE=0
build/tests/cpu-x87.o: CPU_CASE=1
build/tests/cpu-mmx.o: CPU_CASE=2
build/tests/cpu-sse.o: CPU_CASE=3
build/tests/cpu-io.o: CPU_CASE=4

build/tests/cpu-probe.o: tests/cpu-probe.s include/rum/memory_layout.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

$(CPU_OBJECTS): build/tests/cpu-%.o: tests/cpu-kernel.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_CPU_CASE=$(CPU_CASE) -MMD -MP -c $< -o $@

# The observer replaces only C dispatch; entry/return, tables and PIC are real.
build/tests/cpu-%.elf: build/tests/cpu-%.o build/tests/cpu-probe.o $(filter-out build/arch/i386/interrupt.o build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/paging-ok.o: PAGING_CASE=0
build/tests/paging-null.o: PAGING_CASE=1
build/tests/paging-text.o: PAGING_CASE=2
build/tests/paging-rodata.o: PAGING_CASE=3
build/tests/paging-unmapped.o: PAGING_CASE=4
build/tests/paging-readonly.o: PAGING_CASE=5

build/tests/paging-trigger.o: tests/paging-trigger.s
	@mkdir -p $(@D)
	$(AS) $< -o $@

$(PAGING_OBJECTS): build/tests/paging-%.o: tests/paging-kernel.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_PAGING_CASE=$(PAGING_CASE) -MMD -MP -c $< -o $@

build/tests/paging-%.elf: build/tests/paging-%.o build/tests/paging-trigger.o build/tests/paging-spaces.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/fault-de.o: FAULT_CASE=0
build/tests/fault-ud.o: FAULT_CASE=1
build/tests/fault-gp.o: FAULT_CASE=2
build/tests/fault-pf.o: FAULT_CASE=3

build/tests/fault-trigger.o: tests/fault-trigger.s
	@mkdir -p $(@D)
	$(AS) $< -o $@

$(FAULT_OBJECTS): build/tests/fault-%.o: tests/fault-kernel.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_FAULT_CASE=$(FAULT_CASE) -MMD -MP -c $< -o $@

build/tests/fault-%.elf: build/tests/fault-%.o $(FAULT_COMMON) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(FAULT_COMMON) $< -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/console-test: tests/console-test.c kernel/terminal.c include/rum/terminal.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Iinclude tests/console-test.c kernel/terminal.c -o $@

build/tests/memory-test: tests/memory-test.c kernel/memory.c include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Iinclude tests/memory-test.c kernel/memory.c -o $@

build/tests/keyboard-test: tests/keyboard-test.c kernel/keyboard_decode.c include/rum/keyboard_decode.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude tests/keyboard-test.c kernel/keyboard_decode.c -o $@

build/tests/shell-test: tests/shell-test.c kernel/shell.c kernel/terminal.c include/rum/shell.h include/rum/terminal.h include/rum/serial.h tests/include/rum/io.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS) $(SNAKE_HOST_SOURCES) $(SNAKE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Iinclude tests/shell-test.c kernel/shell.c kernel/terminal.c $(SNAKE_HOST_SOURCES) $(STORAGE_HOST_SOURCES) -o $@

build/tests/pmm-test: tests/pmm-test.c kernel/pmm.c include/rum/pmm.h include/rum/multiboot.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Iinclude tests/pmm-test.c kernel/pmm.c -o $@

build/tests/storage-test: tests/storage-test.c tests/storage-checks.c tests/storage-checks.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Iinclude tests/storage-test.c tests/storage-checks.c $(STORAGE_HOST_SOURCES) -o $@

build/tests/snake-test: tests/snake-test.c kernel/snake_model.c include/rum/snake_model.h include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude tests/snake-test.c kernel/snake_model.c -o $@

build/tests/layout-test: tests/layout-test.c $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

build/tests/frame-test: tests/frame-test.c include/rum/interrupts.h include/rum/gdt.h include/rum/cpu_layout.h include/rum/cpu_policy.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

test-host: build/tests/console-test build/tests/memory-test build/tests/keyboard-test build/tests/shell-test build/tests/pmm-test build/tests/storage-test build/tests/snake-test build/tests/layout-test build/tests/frame-test
	./build/tests/console-test
	./build/tests/memory-test
	./build/tests/keyboard-test
	./build/tests/shell-test
	./build/tests/pmm-test
	./build/tests/storage-test
	./build/tests/snake-test
	./build/tests/layout-test
	./build/tests/frame-test
	python3 tests/embed-test.py

doctor:
	bash scripts/doctor.sh

toolchain:
	bash scripts/build-toolchain.sh

clean:
	rm -rf -- build

# The compiler writes these alongside objects; never try to rebuild them alone.
$(DEPENDENCIES) $(TEST_DEPENDENCIES): ;

-include $(DEPENDENCIES) $(TEST_DEPENDENCIES)
