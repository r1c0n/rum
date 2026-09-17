.DEFAULT_GOAL := all

CROSS_PREFIX ?= $(CURDIR)/.tools/cross/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
AS := $(CROSS_PREFIX)as
QEMU ?= qemu-system-i386
HOST_CC ?= gcc

CPPFLAGS := -Iinclude
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Werror \
          -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
          -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -mno-mmx -mno-sse -mno-sse2
LDFLAGS := -T arch/i386/linker.ld -nostdlib -ffreestanding -no-pie \
           -Wl,--build-id=none -Wl,-Map,build/rum.map
SOURCES := kernel/kernel.c kernel/terminal.c kernel/serial.c kernel/memory.c
OBJECTS := build/arch/i386/boot.o $(SOURCES:%.c=build/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all check iso run run-kernel debug test test-host doctor toolchain clean
all: iso

build/arch/i386/boot.o: arch/i386/boot.s
	@mkdir -p $(@D)
	$(AS) $< -o $@

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/rum.elf: $(OBJECTS) arch/i386/linker.ld
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

test: test-host iso
	python3 scripts/smoke-test.py --qemu $(QEMU)

build/tests/console-test: tests/console-test.c kernel/terminal.c include/rum/terminal.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Iinclude tests/console-test.c kernel/terminal.c -o $@

build/tests/memory-test: tests/memory-test.c kernel/memory.c include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Iinclude tests/memory-test.c kernel/memory.c -o $@

test-host: build/tests/console-test build/tests/memory-test
	./build/tests/console-test
	./build/tests/memory-test

doctor:
	bash scripts/doctor.sh

toolchain:
	bash scripts/build-toolchain.sh

clean:
	rm -rf -- build

-include $(DEPENDENCIES)
