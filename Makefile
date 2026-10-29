.DEFAULT_GOAL := all

CROSS_PREFIX ?= $(CURDIR)/.tools/cross/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
AS := $(CROSS_PREFIX)as
OBJCOPY := $(CROSS_PREFIX)objcopy
QEMU ?= qemu-system-i386
DISK_IMAGE ?=
DISK_READ_ONLY ?= 0
DISK_SIZE_MIB ?= 16
export DISK_IMAGE DISK_READ_ONLY DISK_SIZE_MIB
# Read paths from the environment: shell substitution must not evaluate a filename.
QEMU_DISK_ARGS = $${DISK_IMAGE:+--disk "$$DISK_IMAGE"} $$(test "$$DISK_READ_ONLY" = 1 && printf %s --disk-read-only)
HOST_CC ?= gcc

CPPFLAGS := -Ikernel/include -Isdk/include
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Werror \
          -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
          -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -msoft-float -mno-mmx -mno-sse -mno-sse2
LAYOUT_HEADERS := kernel/include/rum/memory_layout.h kernel/include/rum/process_limits.h
ABI_HEADERS := $(wildcard sdk/include/rum/abi/*.h)
LINKER_SCRIPT := build/arch/i386/linker.ld
LDFLAGS := -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie \
           -Wl,--build-id=none -Wl,-Map,build/rum.map
CORE_SOURCES := kernel/core/init.c kernel/core/memory.c
DRIVER_SOURCES := drivers/display/terminal.c drivers/serial/serial.c \
                  drivers/input/keyboard.c \
                  drivers/input/keyboard_decode.c drivers/storage/ata.c
MM_SOURCES := kernel/mm/pmm.c kernel/mm/heap.c
PROCESS_SOURCES := kernel/process/task.c kernel/process/session.c kernel/process/syscall.c kernel/process/handles.c \
                   kernel/process/elf.c kernel/process/process.c
FS_SOURCES := kernel/fs/ramfs.c kernel/fs/system.c kernel/fs/path.c kernel/fs/fs-types.c \
              kernel/fs/fs.c kernel/fs/fat16.c kernel/fs/fat16-write.c
IO_SOURCES := kernel/io/block.c
HAL_SOURCES := hal/pc/pic.c hal/pc/irq.c hal/pc/pit.c
RECOVERY_SOURCES := kernel/recovery/shell.c kernel/recovery/snake.c kernel/recovery/snake_model.c
DEBUG_SOURCES := kernel/debug/diagnostics.c kernel/debug/diagnostics-report.c
ARCH_SOURCES := arch/i386/cpu.c arch/i386/gdt.c arch/i386/interrupt.c \
                arch/i386/exceptions.c \
                arch/i386/paging.c
SOURCES := $(CORE_SOURCES) $(DRIVER_SOURCES) $(MM_SOURCES) \
           $(PROCESS_SOURCES) $(FS_SOURCES) $(IO_SOURCES) \
           $(HAL_SOURCES) $(RECOVERY_SOURCES) \
           $(DEBUG_SOURCES) $(ARCH_SOURCES)
ASM_SOURCES := arch/i386/boot.s arch/i386/interrupts.s arch/i386/context.s
OBJECTS := $(ASM_SOURCES:%.s=build/%.o) build/arch/i386/gdt-load.o $(SOURCES:%.c=build/%.o) build/generated/embedded-files.o
DEPENDENCIES := $(SOURCES:%.c=build/%.d) build/arch/i386/boot.d build/arch/i386/interrupts.d build/arch/i386/gdt-load.d build/generated/embedded-files.d
FAULT_KERNELS := build/tests/fault-de.elf build/tests/fault-ud.elf build/tests/fault-gp.elf build/tests/fault-pf.elf
FAULT_OBJECTS := $(FAULT_KERNELS:.elf=.o)
FAULT_COMMON := $(filter-out build/kernel/core/init.o,$(OBJECTS)) build/tests/fault-trigger.o
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
STORAGE_HOST_SOURCES := kernel/mm/heap.c kernel/fs/ramfs.c kernel/fs/path.c kernel/fs/fs-types.c kernel/core/memory.c tests/page-backend.c build/embedded-files.c
TEST_DEPENDENCIES += build/tests/fs-kernel.d build/tests/fs-checks.d build/tests/fs-backend.d
STORAGE_HOST_HEADERS := kernel/include/rum/heap.h kernel/include/rum/ramfs.h kernel/include/rum/fs.h kernel/include/rum/path.h kernel/include/rum/fs_types.h kernel/include/rum/fs_limits.h $(ABI_HEADERS) kernel/include/rum/embedded.h kernel/include/rum/paging.h kernel/include/rum/pmm.h kernel/include/rum/memory.h tests/page-backend.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
SNAKE_HOST_SOURCES := kernel/recovery/snake.c kernel/recovery/snake_model.c
SNAKE_HOST_HEADERS := kernel/include/rum/snake.h kernel/include/rum/snake_model.h kernel/include/rum/timer.h
SYSTEM_COMMANDS := help about echo clear ls cat pwd cd write mkdir rm run exit recovery
SDK_EXAMPLES := hello nonzero fault spin readline
APP_PROGRAMS := snake
USER_PROGRAMS := shell $(SYSTEM_COMMANDS) $(APP_PROGRAMS) $(SDK_EXAMPLES)
USER_CPPFLAGS := -Isdk/include -Ibuild/user/include
USER_INCLUDE_STAMP := build/user/include/.abi-stamp
USER_CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Werror \
               -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
               -fno-pie -fno-pic -fno-builtin -fno-asynchronous-unwind-tables \
               -msoft-float -mno-mmx -mno-sse -mno-sse2
USER_RUNTIME := build/sdk/lib/start.o build/sdk/lib/syscall-entry.o build/sdk/lib/syscall.o build/sdk/lib/tools.o
USER_DEBUG := $(addprefix build/user/debug/,$(addsuffix .elf,$(USER_PROGRAMS)))
USER_ASSETS := $(addprefix build/user/system/,$(addsuffix .elf,$(USER_PROGRAMS)))
USER_DEPENDENCIES := $(USER_RUNTIME:.o=.d) $(addprefix build/user/objects/,$(addsuffix .d,$(USER_PROGRAMS)))
ABI_CASES := args limits hello
ABI_KERNELS := $(addprefix build/tests/abi-,$(addsuffix .elf,$(ABI_CASES)))
ABI_OBJECTS := $(ABI_KERNELS:.elf=.o)
ABI_IMAGES := $(addprefix build/tests/abi-image-,$(addsuffix .o,$(ABI_CASES)))
TEST_DEPENDENCIES += $(ABI_OBJECTS:.o=.d) $(ABI_IMAGES:.o=.d) build/tests/abi-entry.d build/tests/user/probe.d build/tests/user/probe-entry.d
TEST_DEPENDENCIES += build/tests/file-syscall-kernel.d build/tests/file-syscall-image.d build/tests/user/file-syscall.d
TEST_DEPENDENCIES += build/tests/process-syscall-kernel.d build/tests/process-syscall-image.d build/tests/user/process-syscall.d
TEST_DEPENDENCIES += build/tests/preemption-kernel.d build/tests/preemption-probe.d
TEST_DEPENDENCIES += build/tests/lifecycle-kernel.d build/tests/lifecycle-ids-kernel.d build/tests/user/lifecycle.d
TEST_DEPENDENCIES += build/tests/waits-kernel.d build/tests/sleep-kernel.d build/tests/user/sleep.d
TEST_DEPENDENCIES += build/tests/console-kernel.d build/tests/user/console.d

.PHONY: all check iso user test-user run run-kernel debug panic test test-host test-package doctor toolchain clean FORCE
.SECONDARY: $(FAULT_OBJECTS) $(PAGING_OBJECTS) $(CPU_OBJECTS) $(PROCESS_FAULT_OBJECTS) build/tests/paging-spaces.o build/tests/user-memory.o
.SECONDARY: $(USER_DEBUG) $(USER_RUNTIME) $(addprefix build/user/objects/,$(addsuffix .o,$(USER_PROGRAMS)))
.SECONDARY: $(ABI_OBJECTS) $(ABI_IMAGES) build/tests/elf-loader-image.o
all: user iso

# Userspace has its own startup, include path, flags, objects and linker script.
$(USER_INCLUDE_STAMP): $(ABI_HEADERS) Makefile
	@mkdir -p $(@D)/rum/abi
	cp -- $(ABI_HEADERS) $(@D)/rum/abi/
	touch $@

build/sdk/linker/user.ld: sdk/linker/user.ld $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -E -P -undef -x c -D__ASSEMBLER__ $< -o $@

$(addprefix build/user/objects/,$(addsuffix .o,$(SYSTEM_COMMANDS))): build/user/objects/%.o: system/commands/%.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

$(addprefix build/user/objects/,$(addsuffix .o,$(SDK_EXAMPLES))): build/user/objects/%.o: sdk/examples/%.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/objects/shell.o: system/shell/main.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

$(addprefix build/user/objects/,$(addsuffix .o,$(APP_PROGRAMS))): build/user/objects/%.o: apps/%/main.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/sdk/lib/%.o: sdk/lib/%.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/sdk/lib/start.o: sdk/lib/start.s $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/sdk/lib/syscall-entry.o: sdk/lib/syscall.s $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/user/debug/%.elf: build/user/objects/%.o $(USER_RUNTIME) build/sdk/linker/user.ld
	@mkdir -p $(@D)
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie \
	    -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 \
	    -Wl,-Map,$(@:.elf=.map) $(filter %.o,$^) -lgcc -o $@

build/tools/check-user-elf: sdk/tools/check-user-elf.c $(ABI_HEADERS) $(LAYOUT_HEADERS) kernel/include/rum/ramfs.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $< -o $@

build/user/system/%.elf: build/user/debug/%.elf build/tools/check-user-elf
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@

# Userspace is a separate Multiboot module, never linked into the kernel.
# Runtime files are stripped; symbols stay in build/user/debug/ only.
build/rum-system.img: FORCE $(USER_ASSETS) scripts/pack-system.py
	python3 scripts/pack-system.py build/user/system $@ --files $(notdir $(USER_ASSETS))

user: $(USER_ASSETS) build/rum-system.img
	@$(foreach program,$(USER_PROGRAMS),build/tools/check-user-elf build/user/system/$(program).elf build/user/debug/$(program).elf || exit $$?;)

test-user: user
	python3 tests/user-elf-test.py --cross-prefix $(CROSS_PREFIX)
	python3 tests/system-image-test.py

.PHONY: test-system-volume
test: test-system-volume
test-system-volume: iso
	python3 tests/system-volume-qemu-test.py --qemu $(QEMU)

build/arch/i386/gdt.o: arch/i386/gdt.c Makefile
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

$(LINKER_SCRIPT): arch/i386/linker.ld kernel/include/rum/memory_layout.h Makefile
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

build/rum.iso: build/rum.elf build/rum-system.img boot/grub/grub.cfg | check
	@mkdir -p build/isodir/boot/grub
	cp build/rum.elf build/isodir/boot/rum.elf
	cp build/rum-system.img build/isodir/boot/rum-system.img
	cp boot/grub/grub.cfg build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/isodir

iso: build/rum.iso

# Legacy diagnostics exercise the kernel recovery console deliberately.
build/tests/recovery.iso: build/rum.elf build/rum-system.img Makefile
	@mkdir -p build/recovery-isodir/boot/grub
	cp build/rum.elf build/recovery-isodir/boot/rum.elf
	cp build/rum-system.img build/recovery-isodir/boot/rum-system.img
	printf 'set timeout=0\nmenuentry "rum recovery" {\n multiboot /boot/rum.elf rum.recovery\n module /boot/rum-system.img\n boot\n}\n' > build/recovery-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/recovery-isodir

run:
	python3 scripts/run-qemu.py --validate-disk-only $(QEMU_DISK_ARGS)
	$(MAKE) iso
	python3 scripts/run-qemu.py --qemu $(QEMU) $(QEMU_DISK_ARGS)

run-kernel:
	python3 scripts/run-qemu.py --validate-disk-only $(QEMU_DISK_ARGS)
	$(MAKE) check user
	python3 scripts/run-qemu.py --qemu $(QEMU) --kernel --image build/rum.elf $(QEMU_DISK_ARGS)

debug:
	python3 scripts/run-qemu.py --validate-disk-only $(QEMU_DISK_ARGS)
	$(MAKE) iso
	python3 scripts/run-qemu.py --qemu $(QEMU) --debug $(QEMU_DISK_ARGS)

.PHONY: create-disk
create-disk:
	@test -n "$$DISK_IMAGE" || (echo 'Set DISK_IMAGE to a NEW disposable image path.' >&2; exit 2)
	python3 scripts/disk-image.py "$$DISK_IMAGE" --size-mib "$$DISK_SIZE_MIB"

panic: build/tests/fault-ud.elf
	$(QEMU) -m 64M -kernel $< -serial stdio -no-reboot -no-shutdown

test: test-host test-user test-package test-block build/tests/recovery.iso $(FAULT_KERNELS) build/tests/irq.elf $(CPU_KERNELS) $(PROCESS_FAULT_KERNELS) build/tests/syscall.elf build/tests/elf-loader.elf $(PAGING_KERNELS) build/tests/storage.elf build/tests/fs.elf build/tests/task.elf build/tests/task-fault.elf build/tests/task-double-fault.elf $(ABI_KERNELS)
	python3 scripts/smoke-test.py --qemu $(QEMU)

.PHONY: test-file-syscalls
test: test-file-syscalls

.PHONY: test-preemption
test: test-preemption
test-preemption: build/tests/preemption.elf build/tests/preemption.iso build/tests/timer-test
	./build/tests/timer-test
	python3 tests/preemption-qemu-test.py --qemu $(QEMU)

.PHONY: test-lifecycle
test: test-lifecycle
test-lifecycle: build/tests/lifecycle.elf build/tests/lifecycle.iso build/tests/lifecycle-ids.elf build/tests/lifecycle-ids.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU)

.PHONY: test-sleep
test: test-sleep
test-sleep: build/tests/waits.elf build/tests/waits.iso build/tests/sleep.elf build/tests/sleep.iso build/tests/timer-test
	./build/tests/timer-test
	python3 tests/sleep-qemu-test.py --qemu $(QEMU)

build/tests/session-test: tests/session-test.c kernel/process/session.c kernel/include/rum/session.h $(LAYOUT_HEADERS) $(ABI_HEADERS) tests/include/rum/cpu.h Makefile
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/include $(CPPFLAGS) -std=gnu11 -O2 -Wall -Wextra -Werror tests/session-test.c kernel/process/session.c -o $@

test-host: build/tests/session-test

.PHONY: test-console-sessions
test: test-console-sessions
test-console-sessions: all build/tests/console.elf build/tests/console.iso build/tests/console-supervisor.iso
	python3 tests/console-qemu-test.py --qemu $(QEMU)

build/tests/user/console.o: tests/console-user.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/console.debug.elf: build/tests/user/console.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/console.elf: build/tests/user/console.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/console-image.o: tests/console-image.s build/tests/user/console.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/console.elf: build/tests/console-kernel.o build/tests/console-image.o build/tests/fs-backend.o $(filter-out build/tests/fault-trigger.o build/kernel/process/task.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/console.iso: build/tests/console.elf Makefile
	@mkdir -p build/console-isodir/boot/grub
	cp $< build/console-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum console tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/console-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/console-isodir
build/tests/console-system.img: build/tests/user/console.elf scripts/pack-system.py
	python3 scripts/pack-system.py build/tests/user $@ --files console.elf
build/tests/console-supervisor.iso: build/rum.elf build/tests/console-system.img Makefile
	@mkdir -p build/console-supervisor-isodir/boot/grub
	cp build/rum.elf build/console-supervisor-isodir/boot/rum.elf
	cp build/tests/console-system.img build/console-supervisor-isodir/boot/rum-system.img
	printf 'set timeout=0\nmenuentry "rum console supervisor tests" {\n multiboot /boot/rum.elf rum.shell=/rum/console.elf\n module /boot/rum-system.img\n boot\n}\n' > build/console-supervisor-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/console-supervisor-isodir

build/tests/user/sleep.o: tests/sleep-user.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/sleep.debug.elf: build/tests/user/sleep.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/sleep.elf: build/tests/user/sleep.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/sleep-image.o: tests/sleep-image.s build/tests/user/sleep.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/waits.elf: build/tests/waits-kernel.o $(filter-out build/tests/fault-trigger.o build/kernel/process/task.o build/hal/pc/pit.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/sleep.elf: build/tests/sleep-kernel.o build/tests/sleep-image.o $(filter-out build/tests/fault-trigger.o build/hal/pc/pit.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/waits.iso build/tests/sleep.iso: build/tests/%.iso: build/tests/%.elf Makefile
	@mkdir -p build/$*-isodir/boot/grub
	cp $< build/$*-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum timed wait tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/$*-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/$*-isodir

build/tests/user/lifecycle.o: tests/lifecycle-user.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/lifecycle.debug.elf: build/tests/user/lifecycle.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/lifecycle.elf: build/tests/user/lifecycle.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/lifecycle-image.o: tests/lifecycle-image.s build/tests/user/lifecycle.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/lifecycle.elf: build/tests/lifecycle-kernel.o build/tests/lifecycle-image.o build/tests/fs-backend.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/lifecycle-ids.elf: build/tests/lifecycle-ids-kernel.o $(filter-out build/tests/fault-trigger.o build/kernel/process/task.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/lifecycle.iso build/tests/lifecycle-ids.iso: build/tests/%.iso: build/tests/%.elf Makefile
	@mkdir -p build/$*-isodir/boot/grub
	cp $< build/$*-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum lifecycle tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/$*-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/$*-isodir

build/tests/timer-test: tests/timer-test.c hal/pc/pit.c kernel/include/rum/timer.h kernel/include/rum/task.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/include $(CPPFLAGS) -std=gnu11 -O2 -Wall -Wextra -Werror $< -o $@

build/tests/preemption-probe.o: tests/preemption-probe.s $(LAYOUT_HEADERS) $(ABI_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/preemption.elf: build/tests/preemption-kernel.o build/tests/preemption-probe.o $(filter-out build/tests/fault-trigger.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/preemption.iso: build/tests/preemption.elf Makefile
	@mkdir -p build/preemption-isodir/boot/grub
	cp $< build/preemption-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum preemption tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/preemption-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/preemption-isodir
test-file-syscalls: build/tests/file-syscall.elf
	python3 tests/file-syscall-qemu-test.py --qemu $(QEMU) --ram 16
	python3 tests/file-syscall-qemu-test.py --qemu $(QEMU) --ram 64

build/tests/user/file-syscall.o: tests/file-syscall-user.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/tests/user/file-syscall.debug.elf: build/tests/user/file-syscall.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@

build/tests/user/file-syscall.elf: build/tests/user/file-syscall.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@

build/tests/file-syscall-image.o: build/tests/user/file-syscall.elf
build/tests/file-syscall.elf: build/tests/file-syscall-kernel.o build/tests/file-syscall-image.o build/tests/fs-backend.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/user/process-syscall.o: tests/process-syscall-user.c $(USER_INCLUDE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/process-syscall.debug.elf: build/tests/user/process-syscall.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/process-syscall.elf: build/tests/user/process-syscall.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/process-syscall-image.o: build/tests/user/process-syscall.elf
build/tests/process-syscall.elf: build/tests/process-syscall-kernel.o build/tests/process-syscall-image.o build/tests/fs-backend.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

.PHONY: test-userspace-shell
test: test-userspace-shell
test-userspace-shell: all build/tests/process-syscall.elf
	python3 tests/process-syscall-qemu-test.py --qemu $(QEMU)
	python3 tests/userspace-shell-test.py --qemu $(QEMU)

.PHONY: test-release-integration
test: test-release-integration
test-release-integration: all build/tests/readonly.iso test-package
	python3 tests/release-integration-test.py --qemu $(QEMU)

build/tests/readonly.iso: build/rum.elf build/rum-system.img Makefile
	@mkdir -p build/readonly-isodir/boot/grub
	cp build/rum.elf build/readonly-isodir/boot/rum.elf
	cp build/rum-system.img build/readonly-isodir/boot/rum-system.img
	printf 'set timeout=0\nmenuentry "rum read-only disk" {\n multiboot /boot/rum.elf rum.disk-readonly\n module /boot/rum-system.img\n boot\n}\n' > build/readonly-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/readonly-isodir

test-package: iso
	python3 tests/package-test.py

.PHONY: test-block
test-block: build/tests/block-test build/tests/ata-test build/tests/block.elf iso
	./build/tests/block-test
	./build/tests/ata-test
	python3 tests/disk-tools-test.py --boot-output-checks
	python3 tests/block-qemu-test.py --qemu $(QEMU) --ram 16
	python3 tests/block-qemu-test.py --qemu $(QEMU) --ram 64

TEST_DEPENDENCIES += build/tests/block-kernel.d
build/tests/block.elf: build/tests/block-kernel.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/irq.elf: build/tests/irq-kernel.o build/tests/irq-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/storage.elf: build/tests/storage-kernel.o build/tests/storage-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/fs-image.o: tests/fs-image.s build/user/system/hello.elf build/user/system/fault.elf build/user/system/spin.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@

build/tests/fs.elf: build/tests/fs-kernel.o build/tests/fs-image.o build/tests/fs-checks.o build/tests/fs-backend.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
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

build/tests/user/abi-probe.debug.elf: build/tests/user/probe.o build/tests/user/probe-entry.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
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
build/tests/abi-image-hello.o: build/user/system/hello.elf
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

build/tests/cpu-probe.o: tests/cpu-probe.s kernel/include/rum/memory_layout.h
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

build/tests/elf-loader-image.o: tests/elf-loader-image.s build/user/system/hello.elf Makefile
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

build/tests/console-test: tests/console-test.c drivers/display/terminal.c kernel/include/rum/terminal.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Ikernel/include -Isdk/include tests/console-test.c drivers/display/terminal.c -o $@

build/tests/memory-test: tests/memory-test.c kernel/core/memory.c kernel/include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Ikernel/include -Isdk/include tests/memory-test.c kernel/core/memory.c -o $@

build/tests/keyboard-test: tests/keyboard-test.c drivers/input/keyboard_decode.c kernel/include/rum/keyboard_decode.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include tests/keyboard-test.c drivers/input/keyboard_decode.c -o $@

build/tests/shell-test: tests/shell-test.c kernel/recovery/shell.c kernel/debug/diagnostics-report.c drivers/display/terminal.c kernel/include/rum/shell.h kernel/include/rum/process.h kernel/include/rum/diagnostics.h kernel/include/rum/task.h kernel/include/rum/terminal.h kernel/include/rum/serial.h tests/include/rum/io.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS) $(SNAKE_HOST_SOURCES) $(SNAKE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include tests/shell-test.c kernel/recovery/shell.c kernel/debug/diagnostics-report.c drivers/display/terminal.c $(SNAKE_HOST_SOURCES) $(STORAGE_HOST_SOURCES) -o $@

build/tests/pmm-test: tests/pmm-test.c kernel/mm/pmm.c kernel/include/rum/pmm.h kernel/include/rum/multiboot.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include tests/pmm-test.c kernel/mm/pmm.c -o $@

build/tests/storage-test: tests/storage-test.c tests/storage-checks.c tests/storage-checks.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include tests/storage-test.c tests/storage-checks.c $(STORAGE_HOST_SOURCES) -o $@

build/tests/snake-test: tests/snake-test.c kernel/recovery/snake_model.c kernel/include/rum/snake_model.h kernel/include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include tests/snake-test.c kernel/recovery/snake_model.c -o $@

build/tests/layout-test: tests/layout-test.c $(LAYOUT_HEADERS) $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $< -o $@

build/tests/frame-test: tests/frame-test.c kernel/include/rum/interrupts.h kernel/include/rum/gdt.h kernel/include/rum/cpu_layout.h kernel/include/rum/cpu_policy.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $< -o $@

build/tests/abi-test: tests/abi-test.c $(ABI_HEADERS) $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $< -o $@

build/tests/block-test: tests/block-test.c kernel/io/block.c kernel/include/rum/block.h tests/include/rum/cpu.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Ikernel/include -Isdk/include tests/block-test.c kernel/io/block.c -o $@

build/tests/path-test: tests/path-test.c kernel/fs/path.c kernel/fs/fs-types.c kernel/include/rum/path.h kernel/include/rum/fs_types.h kernel/include/rum/fs_limits.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include tests/path-test.c kernel/fs/path.c kernel/fs/fs-types.c -o $@

build/tests/fs-test: tests/fs-test.c tests/fs-checks.c tests/fs-checks.h tests/fs-backend.c tests/fs-backend.h kernel/fs/fs.c $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include tests/fs-test.c tests/fs-checks.c tests/fs-backend.c kernel/fs/fs.c $(STORAGE_HOST_SOURCES) -o $@

test-host: build/tests/path-test build/tests/fs-test

FAT16_HOST_SOURCES := kernel/fs/fat16.c kernel/fs/fat16-write.c kernel/fs/fs.c kernel/io/block.c
FAT16_HEADERS := kernel/include/rum/fat16.h kernel/fs/fat16-internal.h
build/tests/fat16-write-test: tests/fat16-write-test.c tests/fat16-write-checks.c tests/fat16-write-checks.h $(FAT16_HOST_SOURCES) $(FAT16_HEADERS) $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include tests/fat16-write-test.c tests/fat16-write-checks.c $(FAT16_HOST_SOURCES) $(STORAGE_HOST_SOURCES) -o $@
build/tests/fat16-test: tests/fat16-test.c tests/fat16-checks.c tests/fat16-checks.h $(FAT16_HOST_SOURCES) $(FAT16_HEADERS) $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include tests/fat16-test.c tests/fat16-checks.c $(FAT16_HOST_SOURCES) $(STORAGE_HOST_SOURCES) -o $@

.PHONY: test-fat16-host
test-fat16-host: build/tests/fat16-test
	python3 tests/fat16-host-test.py

test-host: test-fat16-host
test-host: test-fat16-write-host
.PHONY: test-fat16-write-host
test-fat16-write-host: build/tests/fat16-write-test
	python3 tests/fat16-write-host-test.py
test: test-fat16-write
.PHONY: test-fat16-write
test-fat16-write: test-fat16-write-host build/tests/fat16-write.elf
	python3 tests/fat16-write-qemu-test.py --qemu $(QEMU) --ram 16 --faults
	python3 tests/fat16-write-qemu-test.py --qemu $(QEMU) --ram 64

TEST_DEPENDENCIES += build/tests/fat16-write-kernel.d build/tests/fat16-write-checks.d
build/tests/fat16-write.elf: build/tests/fat16-write-kernel.o build/tests/fat16-write-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
test: test-fat16
.PHONY: test-fat16
test-fat16: test-fat16-host build/tests/fat16.elf iso
	python3 tests/fat16-qemu-test.py --qemu $(QEMU) --ram 16
	python3 tests/fat16-qemu-test.py --qemu $(QEMU) --ram 64

TEST_DEPENDENCIES += build/tests/fat16-kernel.d build/tests/fat16-checks.d
build/tests/fat16.elf: build/tests/fat16-kernel.o build/tests/fat16-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

.PHONY: test-fs
test-fs: build/tests/path-test build/tests/fs-test build/tests/fs.elf
	./build/tests/path-test
	./build/tests/fs-test
	python3 scripts/smoke-test.py --qemu $(QEMU) --filesystem-only

build/tests/ata-test: tests/ata-test.c drivers/storage/ata.c kernel/io/block.c kernel/include/rum/ata.h kernel/include/rum/block.h tests/ata-include/rum/io.h tests/ata-include/rum/cpu.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/ata-include -Ikernel/include -Isdk/include tests/ata-test.c drivers/storage/ata.c kernel/io/block.c -o $@

test-host: build/tests/ata-test

test-host: build/tests/block-test build/tests/console-test build/tests/memory-test build/tests/keyboard-test build/tests/shell-test build/tests/pmm-test build/tests/storage-test build/tests/snake-test build/tests/layout-test build/tests/frame-test build/tests/abi-test
	./build/tests/session-test
	./build/tests/path-test
	./build/tests/fs-test
	./build/tests/ata-test
	./build/tests/block-test
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
	python3 tests/disk-tools-test.py

doctor:
	bash scripts/doctor.sh

toolchain:
	bash scripts/build-toolchain.sh

clean:
	rm -rf -- build

# The compiler writes these alongside objects; never try to rebuild them alone.
$(DEPENDENCIES) $(TEST_DEPENDENCIES) $(USER_DEPENDENCIES): ;

-include $(DEPENDENCIES) $(TEST_DEPENDENCIES) $(USER_DEPENDENCIES)
