DRIVER_NAME := $(notdir $(patsubst %/,%,$(abspath $(DRIVER_DIR))))
REPO_ROOT := ../../..

.PHONY: all info kernel

all: kernel

info:
	@echo "linked driver: $(DRIVER_NAME)"
	@echo "manifest: $(DRIVER_MANIFEST)"

kernel:
	$(MAKE) -C $(REPO_ROOT) ./build/driver_builtin64.o
