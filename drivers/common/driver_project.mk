DRIVER_DIR := $(abspath $(DRIVER_DIR))
REPO_ROOT := $(abspath $(DRIVER_DIR)/../../..)
DRIVER_TOOL := $(REPO_ROOT)/tools/driver_project.py

.PHONY: all artifact linked package info

all: artifact

artifact linked package info:
	python3 $(DRIVER_TOOL) --driver-dir $(DRIVER_DIR) --action $@
