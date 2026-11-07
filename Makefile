.DEFAULT_GOAL := all

include mk/config.mk
include mk/kernel.mk
include mk/user.mk
include mk/tests.mk
include mk/commands.mk

.PHONY: all check iso user
all: user iso

# Compiler-generated dependency files are optional on the first build.
$(DEPENDENCIES) $(TEST_DEPENDENCIES) $(USER_DEPENDENCIES): ;
-include $(DEPENDENCIES) $(TEST_DEPENDENCIES) $(USER_DEPENDENCIES)
