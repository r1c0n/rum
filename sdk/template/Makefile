RUM_SDK ?= $(error Set RUM_SDK to the unpacked rum-sdk directory)
CROSS_PREFIX ?= i686-elf-
PYTHON ?= python3
.PHONY: all
all:
	$(PYTHON) "$(RUM_SDK)/tools/build.py" --cross-prefix "$(CROSS_PREFIX)" --output build/example.elf main.c
