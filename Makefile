.DEFAULT_GOAL := all

CROSS_PREFIX ?= $(CURDIR)/.tools/cross/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
AS := $(CROSS_PREFIX)as
OBJCOPY := $(CROSS_PREFIX)objcopy
QEMU ?= qemu-system-i386
HOST_CC ?= gcc

CPPFLAGS := -Iinclude
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Werror \
          -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
          -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -msoft-float -mno-mmx -mno-sse -mno-sse2
LAYOUT_HEADERS := include/rum/memory_layout.h include/rum/process_limits.h
ABI_HEADERS := $(wildcard include/rum/abi/*.h)
LINKER_SCRIPT := build/arch/i386/linker.ld
LDFLAGS := -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie \
           -Wl,--build-id=none -Wl,-Map,build/rum.map
CORE_SOURCES := kernel/core/kernel.c kernel/core/memory.c
DRIVER_SOURCES := kernel/drivers/terminal.c kernel/drivers/serial.c \
                  kernel/drivers/timer.c kernel/drivers/keyboard.c \
                  kernel/drivers/keyboard_decode.c
MM_SOURCES := kernel/mm/pmm.c kernel/mm/heap.c
PROCESS_SOURCES := kernel/process/task.c kernel/process/syscall.c \
                   kernel/process/elf.c kernel/process/process.c
FS_SOURCES := kernel/fs/ramfs.c
UI_SOURCES := kernel/ui/shell.c kernel/ui/snake.c kernel/ui/snake_model.c
DEBUG_SOURCES := kernel/debug/diagnostics.c kernel/debug/diagnostics-report.c
ARCH_SOURCES := arch/i386/cpu.c arch/i386/gdt.c arch/i386/interrupt.c \
                arch/i386/exceptions.c arch/i386/pic.c arch/i386/irq.c \
                arch/i386/paging.c
SOURCES := $(CORE_SOURCES) $(DRIVER_SOURCES) $(MM_SOURCES) \
           $(PROCESS_SOURCES) $(FS_SOURCES) $(UI_SOURCES) \
           $(DEBUG_SOURCES) $(ARCH_SOURCES)
ASM_SOURCES := arch/i386/boot.s arch/i386/interrupts.s arch/i386/context.s
OBJECTS := $(ASM_SOURCES:%.s=build/%.o) build/arch/i386/gdt-load.o $(SOURCES:%.c=build/%.o) build/generated/embedded-files.o
DEPENDENCIES := $(SOURCES:%.c=build/%.d) build/arch/i386/boot.d build/arch/i386/interrupts.d build/arch/i386/gdt-load.d build/generated/embedded-files.d
FAULT_KERNELS := build/tests/fault-de.elf build/tests/fault-ud.elf build/tests/fault-gp.elf build/tests/fault-pf.elf
FAULT_OBJECTS := $(FAULT_KERNELS:.elf=.o)
FAULT_COMMON := $(filter-out build/kernel/core/kernel.o,$(OBJECTS)) build/tests/fault-trigger.o
PAGING_CASES := ok null text rodata unmapped readonly
PAGING_KERNELS := $(addprefix build/tests/paging-,$(addsuffix .elf,$(PAGING_CASES)))
PAGING_OBJECTS := $(PAGING_KERNELS:.elf=.o)
CPU_CASES := irq x87 mmx sse io
CPU_KERNELS := $(addprefix build/tests/cpu-,$(addsuffix .elf,$(CPU_CASES)))
CPU_OBJECTS := $(CPU_KERNELS:.elf=.o)
PROCESS_FAULT_CASES := null kernel readonly ud2 privileged io irq
PROCESS_FAULT_KERNELS := $(addprefix build/tests/process-fault-,$(addsuffix .elf,$(PROCESS_FAULT_CASES)))
PROCESS_FAULT_OBJECTS := $(PROCESS_FAULT_KERNELS:.elf=.o)
TEST_DEPENDENCIES := $(FAULT_OBJECTS:.o=.d) $(PAGING_OBJECTS:.o=.d) $(CPU_OBJECTS:.o=.d) $(PROCESS_FAULT_OBJECTS:.o=.d) build/tests/cpu-probe.d build/tests/process-fault-probe.d build/tests/syscall-kernel.d build/tests/syscall-probe.d build/tests/elf-loader-kernel.d build/tests/elf-loader-image.d build/tests/paging-spaces.d build/tests/user-memory.d build/tests/irq-kernel.d build/tests/storage-kernel.d build/tests/storage-checks.d build/tests/task-kernel.d build/tests/task-fault-kernel.d build/tests/task-double-fault-kernel.d build/tests/task-stack-fault.d
STORAGE_HOST_SOURCES := kernel/mm/heap.c kernel/fs/ramfs.c kernel/core/memory.c tests/page-backend.c build/embedded-files.c
STORAGE_HOST_HEADERS := include/rum/heap.h include/rum/ramfs.h include/rum/embedded.h include/rum/paging.h include/rum/pmm.h include/rum/memory.h tests/page-backend.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
SNAKE_HOST_SOURCES := kernel/ui/snake.c kernel/ui/snake_model.c
SNAKE_HOST_HEADERS := include/rum/snake.h include/rum/snake_model.h include/rum/timer.h
USER_PROGRAMS := hello nonzero fault spin
USER_CPPFLAGS := -Iuser/include -Ibuild/user/include
USER_INCLUDE_STAMP := build/user/include/.abi-stamp
USER_CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Werror \
               -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
               -fno-pie -fno-pic -fno-builtin -fno-asynchronous-unwind-tables \
               -msoft-float -mno-mmx -mno-sse -mno-sse2
USER_RUNTIME := build/user/lib/start.o build/user/lib/syscall-entry.o build/user/lib/syscall.o
USER_DEBUG := $(addprefix build/user/debug/,$(addsuffix .elf,$(USER_PROGRAMS)))
USER_ASSETS := $(addprefix build/user/ramfs/,$(addsuffix .elf,$(USER_PROGRAMS)))
USER_DEPENDENCIES := $(USER_RUNTIME:.o=.d) $(addprefix build/user/programs/,$(addsuffix .d,$(USER_PROGRAMS)))
ABI_CASES := args limits hello
ABI_KERNELS := $(addprefix build/tests/abi-,$(addsuffix .elf,$(ABI_CASES)))
ABI_OBJECTS := $(ABI_KERNELS:.elf=.o)
ABI_IMAGES := $(addprefix build/tests/abi-image-,$(addsuffix .o,$(ABI_CASES)))
TEST_DEPENDENCIES += $(ABI_OBJECTS:.o=.d) $(ABI_IMAGES:.o=.d) build/tests/abi-entry.d build/tests/user/probe.d build/tests/user/probe-entry.d

.PHONY: all check iso user test-user run run-kernel debug panic test test-host test-package doctor toolchain clean FORCE
.SECONDARY: $(FAULT_OBJECTS) $(PAGING_OBJECTS) $(CPU_OBJECTS) $(PROCESS_FAULT_OBJECTS) build/tests/paging-spaces.o build/tests/user-memory.o
.SECONDARY: $(USER_DEBUG) $(USER_RUNTIME) $(addprefix build/user/programs/,$(addsuffix .o,$(USER_PROGRAMS)))
.SECONDARY: $(ABI_OBJECTS) $(ABI_IMAGES) build/tests/elf-loader-image.o
all: user iso

# Userspace has its own startup, include path, flags, objects and linker script.
$(USER_INCLUDE_STAMP): $(ABI_HEADERS) Makefile
	@mkdir -p $(@D)/rum/abi
	cp -- $(ABI_HEADERS) $(@D)/rum/abi/
	touch $@

build/user/linker.ld: user/linker.ld $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -E -P -undef -x c -D__ASSEMBLER__ $< -o $@

build/user/%.o: user/%.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/lib/start.o: user/lib/start.s $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/user/lib/syscall-entry.o: user/lib/syscall.s $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/user/debug/%.elf: build/user/programs/%.o $(USER_RUNTIME) build/user/linker.ld
	@mkdir -p $(@D)
	$(CC) -T build/user/linker.ld -nostdlib -static -no-pie \
	    -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 \
	    -Wl,-Map,$(@:.elf=.map) $(filter %.o,$^) -lgcc -o $@

build/tools/check-user-elf: scripts/check-user-elf.c $(ABI_HEADERS) $(LAYOUT_HEADERS) include/rum/ramfs.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

build/user/ramfs/%.elf: build/user/debug/%.elf build/tools/check-user-elf
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@

# Generate and validate the combined boot-asset manifest during user-only builds.
# Runtime files are stripped; symbols stay in build/user/debug/ only.
build/user/embedded-files.c: FORCE $(USER_ASSETS) scripts/embed-files.py $(wildcard assets/ramfs/*)
	python3 scripts/embed-files.py assets/ramfs $@ --extra-directory build/user/ramfs

user: $(USER_ASSETS) build/user/embedded-files.c
	@$(foreach program,$(USER_PROGRAMS),build/tools/check-user-elf build/user/ramfs/$(program).elf build/user/debug/$(program).elf || exit $$?;)

test-user: user
	python3 tests/user-elf-test.py --cross-prefix $(CROSS_PREFIX)

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

# Check both asset directories each build, including removed files. Unchanged C
# keeps its mtime. Runtime ELFs are stripped; debug symbols and maps stay out.
FORCE:
build/embedded-files.c: FORCE $(USER_ASSETS) scripts/embed-files.py $(wildcard assets/ramfs/*)
	python3 scripts/embed-files.py assets/ramfs $@ --extra-directory build/user/ramfs

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

test: test-host test-user test-package $(FAULT_KERNELS) build/tests/irq.elf $(CPU_KERNELS) $(PROCESS_FAULT_KERNELS) build/tests/syscall.elf build/tests/elf-loader.elf $(PAGING_KERNELS) build/tests/storage.elf build/tests/task.elf build/tests/task-fault.elf build/tests/task-double-fault.elf $(ABI_KERNELS)
	python3 scripts/smoke-test.py --qemu $(QEMU)

test-package: iso
	python3 tests/package-test.py

build/tests/irq.elf: build/tests/irq-kernel.o build/tests/irq-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/storage.elf: build/tests/storage-kernel.o build/tests/storage-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/task.elf: build/tests/task-kernel.o build/tests/task-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/task-fault.elf: build/tests/task-fault-kernel.o $(FAULT_COMMON) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/task-stack-fault.o: tests/task-stack-fault.s $(LAYOUT_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/task-double-fault.elf: build/tests/task-double-fault-kernel.o build/tests/task-stack-fault.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
# Isolated ring-3 fixtures consume the actual separate user ELF/startup/runtime.
build/tests/user/probe.o: tests/user-probe.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/tests/user/probe-entry.o: tests/user-probe-entry.s $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/user/abi-probe.debug.elf: build/tests/user/probe.o build/tests/user/probe-entry.o $(USER_RUNTIME) build/user/linker.ld
	$(CC) -T build/user/linker.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@

build/tests/user/abi-probe.elf: build/tests/user/abi-probe.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@

build/tests/abi-args.o build/tests/abi-image-args.o: USER_CASE=0
build/tests/abi-limits.o build/tests/abi-image-limits.o: USER_CASE=1
build/tests/abi-hello.o build/tests/abi-image-hello.o: USER_CASE=2

$(ABI_OBJECTS): build/tests/abi-%.o: tests/abi-kernel.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_USER_CASE=$(USER_CASE) -MMD -MP -c $< -o $@

build/tests/abi-entry.o: tests/abi-entry.s $(ABI_HEADERS) $(LAYOUT_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/abi-image-args.o build/tests/abi-image-limits.o: build/tests/user/abi-probe.elf
build/tests/abi-image-hello.o: build/user/ramfs/hello.elf
$(ABI_IMAGES): build/tests/abi-image-%.o: tests/abi-image.s Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -DRUM_USER_CASE=$(USER_CASE) -MMD -MP -c $< -o $@

build/tests/abi-%.elf: build/tests/abi-%.o build/tests/abi-image-%.o build/tests/abi-entry.o $(filter-out build/arch/i386/interrupt.o build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
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

build/tests/process-fault-null.o: PROCESS_FAULT_CASE=0
build/tests/process-fault-kernel.o: PROCESS_FAULT_CASE=1
build/tests/process-fault-readonly.o: PROCESS_FAULT_CASE=2
build/tests/process-fault-ud2.o: PROCESS_FAULT_CASE=3
build/tests/process-fault-privileged.o: PROCESS_FAULT_CASE=4
build/tests/process-fault-io.o: PROCESS_FAULT_CASE=5
build/tests/process-fault-irq.o: PROCESS_FAULT_CASE=6

$(PROCESS_FAULT_OBJECTS): build/tests/process-fault-%.o: tests/process-fault-kernel.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_PROCESS_FAULT_CASE=$(PROCESS_FAULT_CASE) -MMD -MP -c $< -o $@

build/tests/process-fault-probe.o: tests/process-fault-probe.s $(LAYOUT_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/process-fault-%.elf: build/tests/process-fault-%.o build/tests/process-fault-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/syscall-probe.o: tests/syscall-probe.s $(ABI_HEADERS) $(LAYOUT_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/syscall.elf: build/tests/syscall-kernel.o build/tests/syscall-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/elf-loader-image.o: tests/elf-loader-image.s build/user/ramfs/hello.elf Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/elf-loader.elf: build/tests/elf-loader-kernel.o build/tests/elf-loader-image.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
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

build/tests/paging-ok.elf: build/tests/user-memory.o

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

build/tests/console-test: tests/console-test.c kernel/drivers/terminal.c include/rum/terminal.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Iinclude tests/console-test.c kernel/drivers/terminal.c -o $@

build/tests/memory-test: tests/memory-test.c kernel/core/memory.c include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Iinclude tests/memory-test.c kernel/core/memory.c -o $@

build/tests/keyboard-test: tests/keyboard-test.c kernel/drivers/keyboard_decode.c include/rum/keyboard_decode.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude tests/keyboard-test.c kernel/drivers/keyboard_decode.c -o $@

build/tests/shell-test: tests/shell-test.c kernel/ui/shell.c kernel/debug/diagnostics-report.c kernel/drivers/terminal.c include/rum/shell.h include/rum/process.h include/rum/diagnostics.h include/rum/task.h include/rum/terminal.h include/rum/serial.h tests/include/rum/io.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS) $(SNAKE_HOST_SOURCES) $(SNAKE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Iinclude tests/shell-test.c kernel/ui/shell.c kernel/debug/diagnostics-report.c kernel/drivers/terminal.c $(SNAKE_HOST_SOURCES) $(STORAGE_HOST_SOURCES) -o $@

build/tests/pmm-test: tests/pmm-test.c kernel/mm/pmm.c include/rum/pmm.h include/rum/multiboot.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Iinclude tests/pmm-test.c kernel/mm/pmm.c -o $@

build/tests/storage-test: tests/storage-test.c tests/storage-checks.c tests/storage-checks.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Iinclude tests/storage-test.c tests/storage-checks.c $(STORAGE_HOST_SOURCES) -o $@

build/tests/snake-test: tests/snake-test.c kernel/ui/snake_model.c include/rum/snake_model.h include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude tests/snake-test.c kernel/ui/snake_model.c -o $@

build/tests/layout-test: tests/layout-test.c $(LAYOUT_HEADERS) $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

build/tests/frame-test: tests/frame-test.c include/rum/interrupts.h include/rum/gdt.h include/rum/cpu_layout.h include/rum/cpu_policy.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

build/tests/abi-test: tests/abi-test.c $(ABI_HEADERS) $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Iinclude $< -o $@

test-host: build/tests/console-test build/tests/memory-test build/tests/keyboard-test build/tests/shell-test build/tests/pmm-test build/tests/storage-test build/tests/snake-test build/tests/layout-test build/tests/frame-test build/tests/abi-test
	./build/tests/console-test
	./build/tests/memory-test
	./build/tests/keyboard-test
	./build/tests/shell-test
	./build/tests/pmm-test
	./build/tests/storage-test
	./build/tests/snake-test
	./build/tests/layout-test
	./build/tests/frame-test
	./build/tests/abi-test
	python3 tests/embed-test.py

doctor:
	bash scripts/doctor.sh

toolchain:
	bash scripts/build-toolchain.sh

clean:
	rm -rf -- build

# The compiler writes these alongside objects; never try to rebuild them alone.
$(DEPENDENCIES) $(TEST_DEPENDENCIES) $(USER_DEPENDENCIES): ;

-include $(DEPENDENCIES) $(TEST_DEPENDENCIES) $(USER_DEPENDENCIES)
