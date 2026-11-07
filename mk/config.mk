CROSS_PREFIX ?= $(CURDIR)/.tools/cross/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
AS := $(CROSS_PREFIX)as
OBJCOPY := $(CROSS_PREFIX)objcopy
QEMU ?= qemu-system-i386
DISK_IMAGE ?=
DISK_READ_ONLY ?= 0
DISK_SIZE_MIB ?= 16
export DISK_IMAGE DISK_READ_ONLY DISK_SIZE_MIB
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
