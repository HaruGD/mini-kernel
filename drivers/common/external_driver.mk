DRIVER_NAME := $(notdir $(patsubst %/,%,$(abspath $(DRIVER_DIR))))
REPO_ROOT := ../../..

.PHONY: all info package

all: package

info:
	@echo "packaged driver: $(DRIVER_NAME)"
	@echo "manifest: $(DRIVER_MANIFEST)"

package:
	$(MAKE) -C $(REPO_ROOT) ./bin/$(DRIVER_NAME).drv
