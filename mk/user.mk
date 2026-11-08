USER_RULES := Makefile mk/config.mk mk/user.mk
.PHONY: user test-user

SYSTEM_COMMANDS := help about echo clear ls cat pwd cd write mkdir rm run exit recovery ps kill sleep jobs fg wait
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
# Userspace has its own startup, include path, flags, objects and linker script.
$(USER_INCLUDE_STAMP): $(ABI_HEADERS) $(USER_RULES)
	@mkdir -p $(@D)/rum/abi
	cp -- $(ABI_HEADERS) $(@D)/rum/abi/
	touch $@

build/sdk/linker/user.ld: sdk/linker/user.ld $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -E -P -undef -x c -D__ASSEMBLER__ $< -o $@

$(addprefix build/user/objects/,$(addsuffix .o,$(SYSTEM_COMMANDS))): build/user/objects/%.o: system/commands/%.c $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

$(addprefix build/user/objects/,$(addsuffix .o,$(SDK_EXAMPLES))): build/user/objects/%.o: sdk/examples/%.c $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/objects/shell.o: system/shell/main.c $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/objects/shell-parse.o: system/shell/parse.c system/shell/shell.h $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/objects/shell-pipeline.o: system/shell/pipeline.c system/shell/shell.h $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/debug/shell.elf: build/user/objects/shell-parse.o build/user/objects/shell-pipeline.o
USER_DEPENDENCIES += build/user/objects/shell-parse.d build/user/objects/shell-pipeline.d

build/user/objects/snake-model.o: apps/snake/model.c apps/snake/model.h $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/user/debug/snake.elf: build/user/objects/snake-model.o
USER_DEPENDENCIES += build/user/objects/snake-model.d

$(addprefix build/user/objects/,$(addsuffix .o,$(APP_PROGRAMS))): build/user/objects/%.o: apps/%/main.c $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/sdk/lib/%.o: sdk/lib/%.c $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

build/sdk/lib/start.o: sdk/lib/start.s $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/sdk/lib/syscall-entry.o: sdk/lib/syscall.s $(USER_INCLUDE_STAMP) $(USER_RULES)
	@mkdir -p $(@D)
	$(CC) $(USER_CPPFLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

build/user/debug/%.elf: build/user/objects/%.o $(USER_RUNTIME) build/sdk/linker/user.ld
	@mkdir -p $(@D)
	$(CC) -T build/sdk/linker/user.ld -nostdlib -static -no-pie \
	    -Wl,--build-id=none -Wl,--no-undefined -Wl,-z,max-page-size=0x1000 \
	    -Wl,-Map,$(@:.elf=.map) $(filter %.o,$^) -lgcc -o $@

build/tools/check-user-elf: sdk/tools/check-user-elf.c $(ABI_HEADERS) $(LAYOUT_HEADERS) sdk/include/rum/abi/elf.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Ikernel/include -Isdk/include $(sort $(filter %.c,$^)) -o $@

build/user/system/%.elf: build/user/debug/%.elf build/tools/check-user-elf
	@mkdir -p $(@D)
	$(OBJCOPY) --strip-all $< $@.tmp
	build/tools/check-user-elf $@.tmp $<
	mv -- $@.tmp $@

user: $(USER_ASSETS) build/user/programs.txt

build/user/programs.txt: $(USER_ASSETS) $(USER_RULES)
	printf '%s\n' $(notdir $(USER_ASSETS)) > $@.tmp
	mv -- $@.tmp $@

test-user: user
	python3 tests/user-elf-test.py --cross-prefix $(CROSS_PREFIX)

