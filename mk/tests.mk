TESTS_RULES := Makefile mk/config.mk mk/tests.mk
.PHONY: test-authority
test-authority: all test-authority-host build/tests/system-authority.elf build/tests/system-authority.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --authority-only
	python3 tests/system-authority-qemu-test.py --qemu $(QEMU)
test-build test-lifecycle: build/tests/system-authority.elf build/tests/system-authority.iso
build/tests/user/system-authority.o: tests/system-authority-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/system-authority.debug.elf: build/tests/user/system-authority.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/system-authority.elf: build/tests/user/system-authority.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
build/tests/system-authority-image.o: tests/system-authority-image.s build/tests/user/system-authority.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/system-authority.iso: build/tests/system-authority.elf $(TESTS_RULES)
	@mkdir -p build/system-authority-isodir/boot/grub
	cp $< build/system-authority-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum system authority" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/system-authority-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/system-authority-isodir
.PHONY: test-process-objects-host test-process-objects
build/tests/process-reference-test: tests/process-reference-test.c kernel/process/reference.c kernel/object/object.c kernel/include/rum/process_reference.h kernel/include/rum/object.h kernel/include/rum/task.h tests/handles-include/rum/cpu.h $(ABI_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/handles-include -Ikernel/include -Isdk/include $(filter %.c,$^) -o $@
test-process-objects-host: build/tests/process-reference-test build/tests/abi-test
	./build/tests/process-reference-test
	./build/tests/abi-test
test-host test-build: build/tests/process-reference-test
test-process-objects: test-process-objects-host build/tests/process-objects.elf build/tests/process-objects.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --process-objects-only
test-build test-lifecycle: build/tests/process-objects.elf build/tests/process-objects.iso
build/tests/user/process-objects.o: tests/process-object-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/process-objects.debug.elf: build/tests/user/process-objects.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/process-objects.elf: build/tests/user/process-objects.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
build/tests/process-objects-image.o: tests/process-objects-image.s build/tests/user/process-objects.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/process-objects.iso: build/tests/process-objects.elf $(TESTS_RULES)
	@mkdir -p build/process-objects-isodir/boot/grub
	cp $< build/process-objects-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum process objects" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/process-objects-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/process-objects-isodir
.PHONY: test-objects-host
build/tests/object-test: tests/object-test.c kernel/object/object.c kernel/include/rum/object.h kernel/include/rum/task.h tests/handles-include/rum/cpu.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/handles-include -Ikernel/include -Isdk/include $(filter %.c,$^) -o $@
test-objects-host: build/tests/object-test
	./build/tests/object-test
test-host test-build: build/tests/object-test
.PHONY: test-object-handles-host
build/tests/object-handle-test: tests/object-handle-test.c kernel/object/handles.c kernel/object/object.c kernel/include/rum/object_handle.h kernel/include/rum/object.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/handles-include -Ikernel/include -Isdk/include $(filter %.c,$^) -o $@
test-object-handles-host: build/tests/object-handle-test
	./build/tests/object-handle-test
test-host test-build: build/tests/object-handle-test
.PHONY: test-object-handles
test-object-handles: test-object-handles-host build/tests/object-handles.elf build/tests/object-handles.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --object-handles-only
test-build test-lifecycle: build/tests/object-handles.elf build/tests/object-handles.iso
TEST_DEPENDENCIES += build/tests/object-handles-kernel.d build/tests/user/object-handles.d
build/tests/user/object-handles.o: tests/object-handle-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/object-handles.debug.elf: build/tests/user/object-handles.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/object-handles.elf: build/tests/user/object-handles.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
build/tests/object-handles-image.o: tests/object-handles-image.s build/tests/user/object-handles.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/object-handles.iso: build/tests/object-handles.elf $(TESTS_RULES)
	@mkdir -p build/object-handles-isodir/boot/grub
	cp $< build/object-handles-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum typed handles" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/object-handles-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/object-handles-isodir
.PHONY: test-objects
test-objects: test-objects-host build/tests/objects.elf build/tests/objects.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --objects-only
test-build test-lifecycle: build/tests/objects.elf build/tests/objects.iso
TEST_DEPENDENCIES += build/tests/objects-kernel.d
build/tests/objects.iso: build/tests/objects.elf $(TESTS_RULES)
	@mkdir -p build/objects-isodir/boot/grub
	cp $< build/objects-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum object tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/objects-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/objects-isodir
.PHONY: test-sdk-host test-sdk
test-sdk-host: sdk
	python3 tests/sdk-package-test.py --cross-prefix $(CROSS_PREFIX)
test-sdk: all test-sdk-host
	python3 tests/sdk-qemu-test.py --qemu $(QEMU)
.PHONY: test-executable-host
build/tests/elf-stream-test: tests/elf-stream-test.c kernel/process/elf.c kernel/include/rum/elf.h kernel/include/rum/paging.h kernel/include/rum/cpu_policy.h sdk/include/rum/elf-image.h $(ABI_HEADERS) $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Ikernel/include -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c,$^) -o $@
test-executable-host: build/tests/elf-stream-test build/tools/check-user-elf
	./build/tests/elf-stream-test
test-host test-build: build/tests/elf-stream-test
.PHONY: test-executables
build/tests/user/large-loader.o: tests/large-loader-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/large-loader-data.o: tests/large-loader-data.s
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/user/large-loader.debug.elf: build/tests/user/large-loader.o build/tests/user/large-loader-data.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/large-loader.elf: build/tests/user/large-loader.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
build/tests/large-loader-image.o: tests/large-loader-image.s build/tests/user/large-loader.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/large-loader.iso: build/tests/large-loader.elf $(TESTS_RULES)
	@mkdir -p build/large-loader-isodir/boot/grub
	cp $< build/large-loader-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum large executable tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/large-loader-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/large-loader-isodir
build/tests/elf-loader.iso: build/tests/elf-loader.elf $(TESTS_RULES)
	@mkdir -p build/elf-loader-isodir/boot/grub
	cp $< build/elf-loader-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum ELF loader tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/elf-loader-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/elf-loader-isodir
test-executables: test-executable-host build/tests/elf-loader.elf build/tests/elf-loader.iso build/tests/large-loader.elf build/tests/large-loader.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --executable-only
test-build test-lifecycle: build/tests/large-loader.elf build/tests/large-loader.iso
RUNTIME_C_SOURCES := sdk/lib/heap.c sdk/lib/string.c sdk/lib/stdio.c sdk/lib/format.c sdk/lib/runtime.c
.PHONY: test-runtime-host
build/tests/runtime-test: tests/runtime-test.c tests/runtime-renames.h $(RUNTIME_C_SOURCES) $(wildcard sdk/include/c/*.h) sdk/include/rum/user.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include/c -Isdk/include -include tests/runtime-renames.h -std=gnu11 -O2 -fno-builtin -Wall -Wextra -Werror $(filter %.c,$^) -o $@
test-runtime-host: build/tests/runtime-test build/tests/timer-test build/tests/abi-test
	./build/tests/runtime-test
	./build/tests/timer-test
	./build/tests/abi-test
test-host: build/tests/runtime-test
test-build: build/tests/runtime-test
.PHONY: test-runtime
build/tests/user/runtime.o: tests/runtime-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/runtime.debug.elf: build/tests/user/runtime.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/runtime.elf: build/tests/user/runtime.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
build/tests/runtime-image.o: tests/runtime-image.s build/tests/user/runtime.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/runtime.iso: build/tests/runtime.elf $(TESTS_RULES)
	@mkdir -p build/runtime-isodir/boot/grub
	cp $< build/runtime-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum runtime tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/runtime-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/runtime-isodir
test-runtime: test-runtime-host build/tests/runtime.elf build/tests/runtime.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --runtime-only
test-build test-lifecycle: build/tests/runtime.elf build/tests/runtime.iso
COMMAND_HOST_NAMES := cat head wc hexdump more help
COMMAND_HOST_OBJECTS := $(addprefix build/tests/command-,$(addsuffix .o,$(COMMAND_HOST_NAMES)))
$(COMMAND_HOST_OBJECTS): build/tests/command-%.o: system/commands/%.c system/lib/stream.h system/lib/pager.h sdk/include/rum/tools.h sdk/include/rum/user.h $(ABI_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror -Dmain=$*_main -c $< -o $@
build/tests/command-tools-test: tests/command-tools-test.c system/lib/stream.c system/lib/pager.c system/lib/stream.h system/lib/pager.h $(COMMAND_HOST_OBJECTS) $(ABI_HEADERS)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c %.o,$^) -o $@
.PHONY: test-shell-tools-host test-command-tools-host
test-command-tools-host: build/tests/command-tools-test
	./build/tests/command-tools-test
test-shell-tools-host: test-command-tools-host build/tests/terminal-session-test
	./build/tests/terminal-session-test
test-host: test-command-tools-host
test-build: build/tests/command-tools-test
.PHONY: test-vm-host
build/tests/vm-test: tests/vm-test.c kernel/mm/vm.c kernel/include/rum/vm.h $(LAYOUT_HEADERS) $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(filter %.c,$^) -o $@
test-vm-host: build/tests/vm-test build/tests/layout-test build/tests/abi-test
	./build/tests/vm-test
	./build/tests/layout-test
	./build/tests/abi-test
test-host: build/tests/vm-test
test-build: build/tests/vm-test
.PHONY: test-vm
test-vm: test-vm-host test-vm-regions build/tests/paging-ok.elf
	python3 scripts/smoke-test.py --qemu $(QEMU) --paging-only

.PHONY: test-vm-regions
build/tests/user/vm-regions.o: tests/vm-regions-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/vm-regions-stack.o: tests/vm-regions-stack.s $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -c $< -o $@
build/tests/user/vm-regions.debug.elf: build/tests/user/vm-regions.o build/tests/user/vm-regions-stack.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/vm-regions.elf: build/tests/user/vm-regions.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
build/tests/vm-regions-image.o: tests/vm-regions-image.s build/tests/user/vm-regions.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/vm-regions.iso: build/tests/vm-regions.elf $(TESTS_RULES)
	@mkdir -p build/vm-regions-isodir/boot/grub
	cp $< build/vm-regions-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum VM region tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/vm-regions-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/vm-regions-isodir
test-vm-regions: build/tests/vm-regions.elf build/tests/vm-regions.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU) --vm-only
test-build: build/tests/vm-regions.elf build/tests/vm-regions.iso
test-lifecycle: build/tests/vm-regions.elf build/tests/vm-regions.iso

build/tests/sync-command.o: system/commands/sync.c sdk/include/rum/tools.h sdk/include/rum/user.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror -Dmain=sync_main -c $< -o $@
build/tests/sync-test: tests/sync-test.c build/tests/sync-command.o $(ABI_HEADERS)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c %.o,$^) -o $@
test-host test-build: build/tests/sync-test
.PHONY: test-file-tools test-file-tools-host test-rename-host
build/tests/file-copy-test: tests/file-copy-test.c system/lib/file-copy.c system/lib/files.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c,$^) -o $@
build/tests/file-move.o: system/commands/mv.c system/lib/files.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror -Dmain=file_move_main -c $< -o $@
build/tests/file-move-test: tests/file-move-test.c build/tests/file-move.o system/lib/files.h $(ABI_HEADERS)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c %.o,$^) -o $@
test-rename-host: build/tests/fat16-write-test
	python3 tests/file-tools-host-test.py
test-file-tools-host: build/tests/file-copy-test build/tests/file-move-test build/tests/sync-test build/tests/fs-test build/tests/handles-test build/tests/abi-test test-rename-host
	./build/tests/file-copy-test
	./build/tests/file-move-test
	./build/tests/sync-test
	./build/tests/fs-test
	./build/tests/handles-test
	./build/tests/abi-test
build/tests/user/file-tools.o: tests/file-tools-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/file-tools.debug.elf: build/tests/user/file-tools.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/file-tools.elf: build/tests/user/file-tools.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
test-file-tools: all test-file-tools-host build/tests/user/file-tools.elf
	python3 tests/file-tools-qemu-test.py --qemu $(QEMU)
test-host: build/tests/file-copy-test build/tests/file-move-test test-rename-host
test-build: build/tests/file-copy-test build/tests/file-move-test build/tests/user/file-tools.elf
TEST_DEPENDENCIES += build/tests/user/file-tools.d
.PHONY: test-editing-host test-editing
build/tests/shell-editor-test: tests/shell-editor-test.c system/shell/editor.c system/shell/shell.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c,$^) -o $@
build/tests/shell-complete-test: tests/shell-complete-test.c system/shell/complete.c system/shell/parse.c system/shell/shell.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c,$^) -o $@
test-editing-host: build/tests/shell-editor-test build/tests/shell-complete-test build/tests/shell-parse-test build/tests/keyboard-test test-terminal-host
	./build/tests/shell-editor-test
	./build/tests/shell-complete-test
	./build/tests/shell-parse-test
	./build/tests/keyboard-test
test-host: build/tests/shell-editor-test build/tests/shell-complete-test
test-build: build/tests/shell-editor-test build/tests/shell-complete-test
build/tests/user/editing.o: tests/editing-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/editing.debug.elf: build/tests/user/editing.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/editing.elf: build/tests/user/editing.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<
test-editing: all test-editing-host build/tests/user/editing.elf
	python3 tests/editing-qemu-test.py --qemu $(QEMU)
test-build: build/tests/user/editing.elf
TEST_DEPENDENCIES += build/tests/user/editing.d
.PHONY: test-terminal-host test-terminal
build/tests/terminal-session-test: tests/terminal-session-test.c kernel/io/terminal.c drivers/display/terminal.c kernel/include/rum/terminal_session.h kernel/include/rum/terminal.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/handles-include -Itests/include -Ikernel/include -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror $(filter %.c,$^) -o $@

test-terminal-host: build/tests/terminal-session-test build/tests/console-test build/tests/snake-test build/tests/abi-test build/tests/timer-test
	./build/tests/terminal-session-test
	./build/tests/console-test
	./build/tests/snake-test
	./build/tests/abi-test
	./build/tests/timer-test

build/tests/user/terminal.o: tests/terminal-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/tests/user/terminal.debug.elf: build/tests/user/terminal.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@

build/tests/user/terminal.elf: build/tests/user/terminal.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<

test-terminal: all test-terminal-host build/tests/user/terminal.elf
	python3 tests/terminal-qemu-test.py --qemu $(QEMU)

test-host: build/tests/terminal-session-test
test-build: build/tests/terminal-session-test
test-build: build/tests/user/terminal.elf
TEST_DEPENDENCIES += build/tests/user/terminal.d
.PHONY: test-jobs-host test-jobs
build/tests/jobs-test: tests/jobs-test.c kernel/process/jobs.c kernel/include/rum/jobs.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/include -Ikernel/include -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror tests/jobs-test.c kernel/process/jobs.c -o $@

test-jobs-host: build/tests/jobs-test build/tests/session-test build/tests/shell-parse-test build/tests/terminal-session-test build/tests/abi-test
	./build/tests/jobs-test
	./build/tests/terminal-session-test
	./build/tests/session-test
	./build/tests/shell-parse-test
	./build/tests/abi-test

test-jobs: all test-jobs-host build/tests/user/workflow.elf
	python3 tests/jobs-qemu-test.py --qemu $(QEMU)

test-host: build/tests/jobs-test
test-build: build/tests/jobs-test
.PHONY: test-workflows-host test-workflows
build/tests/shell-parse-test: tests/shell-parse-test.c system/shell/parse.c system/shell/shell.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -Isdk/include -std=gnu11 -O2 -Wall -Wextra -Werror tests/shell-parse-test.c system/shell/parse.c -o $@

build/tests/user/workflow.o: tests/workflow-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/tests/user/workflow.debug.elf: build/tests/user/workflow.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@

build/tests/user/workflow.elf: build/tests/user/workflow.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@
	build/tools/check-user-elf $@ $<

test-workflows-host: test-shared-handles-host build/tests/session-test build/tests/shell-parse-test
	./build/tests/session-test
	./build/tests/shell-parse-test

test-workflows: all test-workflows-host build/tests/user/workflow.elf
	python3 tests/workflow-qemu-test.py --qemu $(QEMU)

test-host: build/tests/shell-parse-test
test-build: build/tests/shell-parse-test build/tests/user/workflow.elf
TEST_DEPENDENCIES += build/tests/user/workflow.d
.PHONY: test-host test-package
.PHONY: test-shared-handles-host
test-shared-handles-host: build/tests/handles-test build/tests/abi-test
	./build/tests/handles-test
	./build/tests/abi-test

test-host: build/tests/handles-test

.PHONY: test-shared-handles
test-shared-handles: test-shared-handles-host build/tests/shared-handles.elf build/tests/shared-handles.iso
	python3 tests/shared-handles-qemu-test.py --qemu $(QEMU)

FAULT_KERNELS := build/tests/fault-de.elf build/tests/fault-ud.elf build/tests/fault-gp.elf build/tests/fault-pf.elf
FAULT_OBJECTS := $(FAULT_KERNELS:.elf=.o)
FAULT_COMMON := build/tests/fs-backend.o $(filter-out build/kernel/core/init.o,$(OBJECTS)) build/tests/fault-trigger.o
build/tests/system-authority.elf: build/tests/system-authority-kernel.o build/tests/system-authority-image.o $(filter-out build/tests/fault-trigger.o build/kernel/process/syscall.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/process-objects.elf: build/tests/process-objects-kernel.o build/tests/process-objects-image.o $(filter-out build/tests/fault-trigger.o build/kernel/process/task.o build/kernel/process/syscall.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/object-handles.elf: build/tests/object-handles-kernel.o build/tests/object-handles-image.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/objects.elf: build/tests/objects-kernel.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/vm-regions.elf: build/tests/vm-regions-kernel.o build/tests/vm-regions-image.o $(filter-out build/tests/fault-trigger.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/runtime.elf: build/tests/runtime-kernel.o build/tests/runtime-image.o $(filter-out build/tests/fault-trigger.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
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
TEST_DEPENDENCIES += build/tests/user/vm-regions.d build/tests/vm-regions-kernel.d
TEST_DEPENDENCIES += build/tests/system-authority-kernel.d build/tests/user/system-authority.d
TEST_DEPENDENCIES += build/tests/process-objects-kernel.d build/tests/user/process-objects.d
TEST_DEPENDENCIES += build/tests/user/runtime.d build/tests/runtime-kernel.d
TEST_DEPENDENCIES += build/tests/user/large-loader.d build/tests/large-loader-kernel.d
STORAGE_HOST_SOURCES := kernel/mm/heap.c kernel/fs/path.c kernel/fs/fs-types.c kernel/fs/protection.c kernel/fs/install.c kernel/core/memory.c tests/page-backend.c tests/fs-backend.c kernel/fs/fs.c
TEST_DEPENDENCIES += build/tests/fs-kernel.d build/tests/fs-checks.d build/tests/fs-backend.d
STORAGE_HOST_HEADERS := kernel/include/rum/heap.h tests/test-files.h kernel/include/rum/fs.h kernel/include/rum/fs_policy.h kernel/include/rum/path.h kernel/include/rum/fs_types.h kernel/include/rum/fs_limits.h $(ABI_HEADERS) kernel/include/rum/paging.h kernel/include/rum/pmm.h kernel/include/rum/memory.h tests/page-backend.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
.PHONY: test-authority-host
build/tests/authority-test: tests/authority-test.c kernel/process/handles.c kernel/object/object.c kernel/object/handles.c kernel/io/pipe.c kernel/io/block.c tests/pipe-task-backend.c $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/handles-include -Itests/include $(CPPFLAGS) -std=gnu11 -O2 -fno-builtin -Wall -Wextra -Werror $(sort $(filter %.c,$^)) -o $@
test-authority-host: build/tests/authority-test build/tests/shell-test build/tests/block-test
	./build/tests/authority-test
	./build/tests/shell-test
	./build/tests/block-test
test-host test-build: build/tests/authority-test
SNAKE_HOST_SOURCES := kernel/recovery/snake.c apps/snake/model.c
SNAKE_HOST_HEADERS := kernel/include/rum/snake.h apps/snake/model.h kernel/include/rum/timer.h
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
TEST_DEPENDENCIES += build/tests/integration-kernel.d build/tests/user/integration.d
TEST_DEPENDENCIES += build/tests/shared-handles-kernel.d build/tests/user/shared-handles.d
TEST_DEPENDENCIES += build/tests/pipes-kernel.d build/tests/user/pipes.d

.PHONY: test-pipes
test-pipes: test-pipes-host build/tests/pipes.elf build/tests/pipes.iso
	python3 tests/pipes-qemu-test.py --qemu $(QEMU)
build/tests/user/pipes.o: tests/pipes-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/pipes.debug.elf: build/tests/user/pipes.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/pipes.elf: build/tests/user/pipes.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/pipes-image.o: tests/pipes-image.s build/tests/user/pipes.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/pipes.elf: build/tests/pipes-kernel.o build/tests/pipes-image.o $(filter-out build/tests/fault-trigger.o build/kernel/process/syscall.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/pipes.iso: build/tests/pipes.elf $(TESTS_RULES)
	@mkdir -p build/pipes-isodir/boot/grub
	cp $< build/pipes-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum pipes" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/pipes-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/pipes-isodir

build/tests/user/shared-handles.o: tests/shared-handles-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/shared-handles.debug.elf: build/tests/user/shared-handles.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/shared-handles.elf: build/tests/user/shared-handles.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/shared-handles-image.o: tests/shared-handles-image.s build/tests/user/shared-handles.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/shared-handles.elf: build/tests/shared-handles-kernel.o build/tests/shared-handles-image.o $(filter-out build/tests/fault-trigger.o build/kernel/process/syscall.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/shared-handles.iso: build/tests/shared-handles.elf $(TESTS_RULES)
	@mkdir -p build/shared-handles-isodir/boot/grub
	cp $< build/shared-handles-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum shared handles" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/shared-handles-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/shared-handles-isodir

build/tests/handles-test: tests/handles-test.c kernel/process/handles.c kernel/object/object.c kernel/object/handles.c kernel/io/pipe.c tests/pipe-task-backend.c tests/pipe-task-backend.h kernel/include/rum/handles.h kernel/include/rum/pipe.h tests/handles-include/rum/cpu.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/handles-include -Itests/include $(CPPFLAGS) -std=gnu11 -O2 -fno-builtin -Wall -Wextra -Werror $(sort $(filter %.c,$^)) -o $@

.PHONY: test-pipes-host
test-pipes-host: build/tests/pipe-test build/tests/handles-test build/tests/abi-test
	./build/tests/pipe-test
	./build/tests/handles-test
	./build/tests/abi-test
build/tests/pipe-test: tests/pipe-test.c kernel/io/pipe.c kernel/core/memory.c tests/pipe-task-backend.c tests/pipe-task-backend.h kernel/include/rum/pipe.h tests/handles-include/rum/cpu.h $(ABI_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/handles-include $(CPPFLAGS) -std=gnu11 -O2 -fno-builtin -Wall -Wextra -Werror $(sort $(filter %.c,$^)) -o $@
test-host: build/tests/pipe-test

.SECONDARY: $(FAULT_OBJECTS) $(PAGING_OBJECTS) $(CPU_OBJECTS) $(PROCESS_FAULT_OBJECTS) build/tests/paging-spaces.o build/tests/user-memory.o
.SECONDARY: $(USER_DEBUG) $(USER_RUNTIME) $(addprefix build/user/objects/,$(addsuffix .o,$(USER_PROGRAMS)))
.SECONDARY: $(ABI_OBJECTS) $(ABI_IMAGES) build/tests/elf-loader-image.o
# Legacy diagnostics exercise the kernel recovery console deliberately.
build/tests/recovery.iso: build/rum.elf $(TESTS_RULES)
	@mkdir -p build/recovery-isodir/boot/grub
	cp build/rum.elf build/recovery-isodir/boot/rum.elf
	rm -f -- build/recovery-isodir/boot/rum-system.img
	printf 'set timeout=0\nmenuentry "rum recovery" {\n multiboot /boot/rum.elf rum.recovery\n boot\n}\n' > build/recovery-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/recovery-isodir

.PHONY: test-system-disk
test-system-disk: iso
	python3 tests/system-disk-test.py --qemu $(QEMU)

.PHONY: test-file-syscalls

.PHONY: test-preemption
test-preemption: build/tests/preemption.elf build/tests/preemption.iso build/tests/timer-test
	./build/tests/timer-test
	python3 tests/preemption-qemu-test.py --qemu $(QEMU)

.PHONY: test-lifecycle
test-lifecycle: build/tests/lifecycle.elf build/tests/lifecycle.iso build/tests/lifecycle-ids.elf build/tests/lifecycle-ids.iso
	python3 tests/lifecycle-qemu-test.py --qemu $(QEMU)

.PHONY: test-sleep
test-sleep: build/tests/waits.elf build/tests/waits.iso build/tests/sleep.elf build/tests/sleep.iso build/tests/timer-test
	./build/tests/timer-test
	python3 tests/sleep-qemu-test.py --qemu $(QEMU)

build/tests/session-test: tests/session-test.c kernel/process/session.c kernel/include/rum/session.h $(LAYOUT_HEADERS) $(ABI_HEADERS) tests/include/rum/cpu.h $(TESTS_RULES)
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/include $(CPPFLAGS) -std=gnu11 -O2 -Wall -Wextra -Werror $(sort $(filter %.c,$^)) -o $@

test-host: build/tests/session-test build/tests/timer-test

.PHONY: test-console-sessions
.PHONY: test-process-tools
.PHONY: test-integration-stress
test-integration-stress: build/tests/integration.elf build/tests/integration.iso
	python3 tests/integration-stress-test.py --qemu $(QEMU)

build/tests/user/integration.o: tests/integration-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@
build/tests/user/integration.debug.elf: build/tests/user/integration.o $(USER_RUNTIME) build/sdk/linker/user.ld
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie -Wl,--build-id=none \
	    -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 $(filter %.o,$^) -lgcc -o $@
build/tests/user/integration.elf: build/tests/user/integration.debug.elf build/tools/check-user-elf
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@
build/tests/integration-image.o: tests/integration-image.s build/tests/user/integration.elf
	@mkdir -p $(@D)
	$(AS) $< -o $@
build/tests/integration.elf: build/tests/integration-kernel.o build/tests/integration-image.o $(filter-out build/tests/fault-trigger.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/integration.iso: build/tests/integration.elf $(TESTS_RULES)
	@mkdir -p build/integration-isodir/boot/grub
	cp $< build/integration-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum integration stress" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/integration-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/integration-isodir

test-process-tools: all
	python3 tests/process-tools-test.py --qemu $(QEMU)
test-console-sessions: all build/tests/console.elf build/tests/console.iso build/tests/console-supervisor.iso
	python3 tests/console-qemu-test.py --qemu $(QEMU)

build/tests/user/console.o: tests/console-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
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
build/tests/console.iso: build/tests/console.elf $(TESTS_RULES)
	@mkdir -p build/console-isodir/boot/grub
	cp $< build/console-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum console tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/console-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/console-isodir
build/tests/console-supervisor.iso: build/rum.elf build/tests/user/console.elf $(TESTS_RULES)
	@mkdir -p build/console-supervisor-isodir/boot/grub
	cp build/rum.elf build/console-supervisor-isodir/boot/rum.elf
	rm -f -- build/console-supervisor-isodir/boot/rum-system.img
	printf 'set timeout=0\nmenuentry "rum console supervisor tests" {\n multiboot /boot/rum.elf rum.shell=/rum/console.elf\n boot\n}\n' > build/console-supervisor-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/console-supervisor-isodir

build/tests/user/sleep.o: tests/sleep-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
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
build/tests/waits.iso build/tests/sleep.iso: build/tests/%.iso: build/tests/%.elf $(TESTS_RULES)
	@mkdir -p build/$*-isodir/boot/grub
	cp $< build/$*-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum timed wait tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/$*-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/$*-isodir

build/tests/user/lifecycle.o: tests/lifecycle-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
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
build/tests/lifecycle.iso build/tests/lifecycle-ids.iso: build/tests/%.iso: build/tests/%.elf $(TESTS_RULES)
	@mkdir -p build/$*-isodir/boot/grub
	cp $< build/$*-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum lifecycle tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/$*-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/$*-isodir

build/tests/timer-test: tests/timer-test.c hal/pc/pit.c kernel/include/rum/timer.h kernel/include/rum/task.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -Itests/include $(CPPFLAGS) -std=gnu11 -O2 -Wall -Wextra -Werror tests/timer-test.c -o $@

build/tests/preemption-probe.o: tests/preemption-probe.s $(LAYOUT_HEADERS) $(ABI_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/preemption.elf: build/tests/preemption-kernel.o build/tests/preemption-probe.o $(filter-out build/tests/fault-trigger.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/preemption.iso: build/tests/preemption.elf $(TESTS_RULES)
	@mkdir -p build/preemption-isodir/boot/grub
	cp $< build/preemption-isodir/boot/rum.elf
	printf 'set timeout=0\nmenuentry "rum preemption tests" {\n multiboot /boot/rum.elf\n boot\n}\n' > build/preemption-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/preemption-isodir
test-file-syscalls: build/tests/file-syscall.elf
	python3 tests/file-syscall-qemu-test.py --qemu $(QEMU) --ram 16

build/tests/user/file-syscall.o: tests/file-syscall-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
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

build/tests/user/process-syscall.o: tests/process-syscall-user.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
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
test-userspace-shell: all build/tests/process-syscall.elf build/tests/user/large-loader.elf
	python3 tests/process-syscall-qemu-test.py --qemu $(QEMU)
	python3 tests/userspace-shell-test.py --qemu $(QEMU)

.PHONY: test-release-integration
test-release-integration: all build/tests/readonly.iso test-package
	python3 tests/release-integration-test.py --qemu $(QEMU)

build/tests/readonly.iso: build/rum.elf $(TESTS_RULES)
	@mkdir -p build/readonly-isodir/boot/grub
	cp build/rum.elf build/readonly-isodir/boot/rum.elf
	rm -f -- build/readonly-isodir/boot/rum-system.img
	printf 'set timeout=0\nmenuentry "rum read-only disk" {\n multiboot /boot/rum.elf rum.disk-readonly\n boot\n}\n' > build/readonly-isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/readonly-isodir

test-package: iso
	python3 tests/package-test.py

.PHONY: test-block
test-block: build/tests/block-test build/tests/ata-test build/tests/block.elf iso
	./build/tests/block-test
	./build/tests/ata-test
	python3 tests/disk-tools-test.py --boot-output-checks
	python3 tests/block-qemu-test.py --qemu $(QEMU) --ram 16

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

build/tests/task-kernel.o: CFLAGS += -Wframe-larger-than=8192
build/tests/task.elf: build/tests/task-kernel.o build/tests/task-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/task-fault.elf: build/tests/task-fault-kernel.o $(FAULT_COMMON) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/task-stack-fault.o: tests/task-stack-fault.s $(LAYOUT_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/task-double-fault.elf: build/tests/task-double-fault-kernel.o build/tests/task-stack-fault.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
# Isolated ring-3 fixtures consume the actual separate user ELF/startup/runtime.
build/tests/user/probe.o: tests/user-probe.c $(USER_INCLUDE_STAMP) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/tests/user/probe-entry.o: tests/user-probe-entry.s $(USER_INCLUDE_STAMP) $(TESTS_RULES)
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

$(ABI_OBJECTS): build/tests/abi-%.o: tests/abi-kernel.c $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_USER_CASE=$(USER_CASE) -MMD -MP -c $< -o $@

build/tests/abi-entry.o: tests/abi-entry.s $(ABI_HEADERS) $(LAYOUT_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/abi-image-args.o build/tests/abi-image-limits.o: build/tests/user/abi-probe.elf
build/tests/abi-image-hello.o: build/user/system/hello.elf
$(ABI_IMAGES): build/tests/abi-image-%.o: tests/abi-image.s $(TESTS_RULES)
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

$(CPU_OBJECTS): build/tests/cpu-%.o: tests/cpu-kernel.c $(TESTS_RULES)
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

$(PROCESS_FAULT_OBJECTS): build/tests/process-fault-%.o: tests/process-fault-kernel.c $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_PROCESS_FAULT_CASE=$(PROCESS_FAULT_CASE) -MMD -MP -c $< -o $@

build/tests/process-fault-probe.o: tests/process-fault-probe.s $(LAYOUT_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/process-fault-%.elf: build/tests/process-fault-%.o build/tests/process-fault-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/syscall-probe.o: tests/syscall-probe.s $(ABI_HEADERS) $(LAYOUT_HEADERS) $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/syscall.elf: build/tests/syscall-kernel.o build/tests/syscall-probe.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/elf-loader-image.o: tests/elf-loader-image.s build/user/system/hello.elf $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/tests/elf-loader.elf: build/tests/elf-loader-kernel.o build/tests/elf-loader-image.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@
build/tests/large-loader.elf: build/tests/large-loader-kernel.o build/tests/large-loader-image.o $(filter-out build/tests/fault-trigger.o build/arch/i386/interrupt.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
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

$(PAGING_OBJECTS): build/tests/paging-%.o: tests/paging-kernel.c $(TESTS_RULES)
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

$(FAULT_OBJECTS): build/tests/fault-%.o: tests/fault-kernel.c $(TESTS_RULES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRUM_FAULT_CASE=$(FAULT_CASE) -MMD -MP -c $< -o $@

build/tests/fault-%.elf: build/tests/fault-%.o $(FAULT_COMMON) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(FAULT_COMMON) $< -lgcc -o $@
	grub-file --is-x86-multiboot $@

build/tests/console-test: tests/console-test.c drivers/display/terminal.c kernel/include/rum/terminal.h tests/include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

.PHONY: test-power test-power-host
test-host test-build: build/tests/power-test
test-power: all test-power-host
	python3 tests/power-qemu-test.py --qemu $(QEMU)

test-power-host: build/tests/power-test build/tests/console-test build/tests/shell-test
	./build/tests/power-test
	./build/tests/console-test
	./build/tests/shell-test

build/tests/power-test: tests/power-test.c kernel/core/power.c hal/pc/power.c kernel/include/rum/power.h tests/power-include/rum/cpu.h tests/power-include/rum/io.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/power-include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/memory-test: tests/memory-test.c kernel/core/memory.c kernel/include/rum/memory.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/keyboard-test: tests/keyboard-test.c drivers/input/keyboard_decode.c kernel/include/rum/keyboard_decode.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/shell-test: tests/shell-test.c kernel/recovery/shell.c kernel/debug/diagnostics-report.c drivers/display/terminal.c kernel/include/rum/shell.h kernel/include/rum/process.h kernel/include/rum/diagnostics.h kernel/include/rum/task.h kernel/include/rum/terminal.h kernel/include/rum/serial.h tests/include/rum/io.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS) $(SNAKE_HOST_SOURCES) $(SNAKE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/pmm-test: tests/pmm-test.c kernel/mm/pmm.c kernel/include/rum/pmm.h kernel/include/rum/multiboot.h tests/include/rum/cpu.h $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/storage-test: tests/storage-test.c tests/storage-checks.c tests/storage-checks.h $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/snake-test: tests/snake-test.c apps/snake/model.c apps/snake/model.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/layout-test: tests/layout-test.c $(LAYOUT_HEADERS) $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/frame-test: tests/frame-test.c kernel/include/rum/interrupts.h kernel/include/rum/gdt.h kernel/include/rum/cpu_layout.h kernel/include/rum/cpu_policy.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/abi-test: tests/abi-test.c $(ABI_HEADERS) $(LAYOUT_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/block-test: tests/block-test.c kernel/io/block.c kernel/include/rum/block.h tests/include/rum/cpu.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/path-test: tests/path-test.c kernel/fs/path.c kernel/fs/fs-types.c kernel/include/rum/path.h kernel/include/rum/fs_types.h kernel/include/rum/fs_limits.h $(ABI_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/tests/fs-test: tests/fs-test.c tests/fs-checks.c tests/fs-checks.h tests/fs-backend.c tests/fs-backend.h kernel/fs/fs.c $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

test-host: build/tests/path-test build/tests/fs-test

FAT16_HOST_SOURCES := kernel/fs/fat16.c kernel/fs/fat16-write.c kernel/fs/fs.c kernel/io/block.c
FAT16_HEADERS := kernel/include/rum/fat16.h kernel/fs/fat16-internal.h
build/tests/fat16-write-test: tests/fat16-write-test.c tests/fat16-write-checks.c tests/fat16-write-checks.h $(FAT16_HOST_SOURCES) $(FAT16_HEADERS) $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@
build/tests/fat16-test: tests/fat16-test.c tests/fat16-checks.c tests/fat16-checks.h $(FAT16_HOST_SOURCES) $(FAT16_HEADERS) $(STORAGE_HOST_SOURCES) $(STORAGE_HOST_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Itests/include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

.PHONY: test-fat16-host
test-fat16-host: build/tests/fat16-test
	python3 tests/fat16-host-test.py

test-host: test-fat16-host
test-host: test-fat16-write-host
.PHONY: test-fat16-write-host
test-fat16-write-host: build/tests/fat16-write-test
	python3 tests/fat16-write-host-test.py
.PHONY: test-fat16-write
test-fat16-write: test-fat16-write-host build/tests/fat16-write.elf
	python3 tests/fat16-write-qemu-test.py --qemu $(QEMU) --ram 16 --faults

TEST_DEPENDENCIES += build/tests/fat16-write-kernel.d build/tests/fat16-write-checks.d
build/tests/fat16-write.elf: build/tests/fat16-write-kernel.o build/tests/fat16-write-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
.PHONY: test-fat16
test-fat16: test-fat16-host build/tests/fat16.elf iso
	python3 tests/fat16-qemu-test.py --qemu $(QEMU) --ram 16

TEST_DEPENDENCIES += build/tests/fat16-kernel.d build/tests/fat16-checks.d
build/tests/fat16.elf: build/tests/fat16-kernel.o build/tests/fat16-checks.o $(filter-out build/tests/fault-trigger.o,$(FAULT_COMMON)) $(LINKER_SCRIPT)
	$(CC) -T $(LINKER_SCRIPT) -nostdlib -ffreestanding -no-pie -Wl,--build-id=none $(filter %.o,$^) -lgcc -o $@
	grub-file --is-x86-multiboot $@

.PHONY: test-fs
test-fs: build/tests/path-test build/tests/fs-test build/tests/fs.elf
	./build/tests/path-test
	./build/tests/fs-test
	python3 scripts/smoke-test.py --qemu $(QEMU) --filesystem-only

build/tests/ata-test: tests/ata-test.c drivers/storage/ata.c kernel/io/block.c kernel/include/rum/ata.h kernel/include/rum/timer.h kernel/include/rum/block.h tests/ata-include/rum/io.h tests/ata-include/rum/cpu.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Itests/ata-include -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

test-host: build/tests/ata-test

test-host: build/tests/block-test build/tests/console-test build/tests/memory-test build/tests/keyboard-test build/tests/shell-test build/tests/pmm-test build/tests/storage-test build/tests/snake-test build/tests/layout-test build/tests/frame-test build/tests/abi-test
	./build/tests/power-test
	./build/tests/elf-stream-test
	./build/tests/object-test
	./build/tests/process-reference-test
	./build/tests/authority-test
	./build/tests/runtime-test
	./build/tests/object-handle-test
	./build/tests/timer-test
	./build/tests/session-test
	./build/tests/handles-test
	./build/tests/jobs-test
	./build/tests/path-test
	./build/tests/shell-parse-test
	./build/tests/shell-editor-test
	./build/tests/shell-complete-test
	./build/tests/terminal-session-test
	./build/tests/file-copy-test
	./build/tests/file-move-test
	./build/tests/sync-test
	./build/tests/pipe-test
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
	./build/tests/vm-test
	./build/tests/layout-test
	./build/tests/frame-test
	./build/tests/abi-test
	python3 tests/test-runner-test.py
	python3 tests/build-environment-test.py
	python3 tests/disk-tools-test.py

.PHONY: test-build
test-build: user iso build/tests/pipe-test build/tests/pipes.elf build/tests/pipes.iso build/tests/handles-test build/tests/shared-handles.elf build/tests/shared-handles.iso build/tests/recovery.iso $(FAULT_KERNELS) \
    build/tests/irq.elf $(CPU_KERNELS) $(PROCESS_FAULT_KERNELS) build/tests/syscall.elf \
    build/tests/elf-loader.elf $(PAGING_KERNELS) build/tests/storage.elf build/tests/fs.elf \
    build/tests/task.elf build/tests/task-fault.elf build/tests/task-double-fault.elf $(ABI_KERNELS) \
    build/tests/preemption.elf build/tests/preemption.iso build/tests/timer-test build/tests/lifecycle.elf \
    build/tests/lifecycle.iso build/tests/lifecycle-ids.elf build/tests/lifecycle-ids.iso build/tests/waits.elf \
    build/tests/waits.iso build/tests/sleep.elf build/tests/sleep.iso build/tests/session-test \
    build/tests/integration.elf build/tests/integration.iso all build/tests/console.elf \
    build/tests/console.iso build/tests/console-supervisor.iso build/tests/file-syscall.elf build/tests/process-syscall.elf \
    build/tests/readonly.iso build/tests/block-test build/tests/ata-test build/tests/block.elf \
    build/tests/path-test build/tests/fs-test build/tests/fat16-test build/tests/fat16-write-test \
    build/tests/fat16-write.elf build/tests/fat16.elf build/tests/console-test build/tests/memory-test \
    build/tests/keyboard-test build/tests/shell-test build/tests/pmm-test build/tests/storage-test \
    build/tests/snake-test build/tests/layout-test build/tests/frame-test build/tests/abi-test

TEST_JOBS ?= 4
.PHONY: test
test: test-build
	python3 scripts/test.py --qemu $(QEMU) --jobs $(TEST_JOBS) --cross-prefix $(CROSS_PREFIX)
