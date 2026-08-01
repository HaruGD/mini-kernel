# Toolchain
AS = nasm
HOST64_CC = gcc
HOST64_CXX = g++
HOST64_LD = ld
HOST64_OBJCOPY = objcopy
HOST64_AR = ar
UEFI_CC = gcc
UEFI_LD = ld
UEFI_OBJCOPY = objcopy
OVMF_VARS_TEMPLATE = /usr/share/OVMF/OVMF_VARS_4M.fd

# Common flags
INCLUDES = -I./include -I./drivers/fs/fat32/include -I.
HOST64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mgeneral-regs-only -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
HOST64_CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -m64 -mgeneral-regs-only -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables $(INCLUDES)
UEFI_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fshort-wchar -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
USER64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fpie -fno-stack-protector -I./user/include -I./user/sdk/include
DRIVER64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mcmodel=large -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer -I./drivers/include
DRIVER64_CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu++17 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -m64 -mcmodel=large -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer -I./drivers/include

# Userland
USER_ASM_SOURCES = $(wildcard ./user/programs/*.asm)
USER_BINS = $(patsubst ./user/programs/%.asm,./bin/%.bin,$(USER_ASM_SOURCES))
USER_EASM_SOURCES = $(filter-out ./user/programs/user_crt0.easm,$(wildcard ./user/programs/*.easm))
USER_ELF_OBJECTS = $(patsubst ./user/programs/%.easm,./build/user_elf_%.o,$(USER_EASM_SOURCES))
USER_EASM_ELFS = $(patsubst ./user/programs/%.easm,./bin/%.elf,$(USER_EASM_SOURCES))
USER_C_RUNTIME_SOURCES = $(wildcard ./user/programs/user_*.c)
USER_C_SOURCES = $(filter-out $(USER_C_RUNTIME_SOURCES),$(wildcard ./user/programs/*.c))
USER_C_OBJECTS = $(patsubst ./user/programs/%.c,./build/user_c_%.o,$(USER_C_SOURCES))
USER_C_ELFS = $(patsubst ./user/programs/%.c,./bin/%.elf,$(USER_C_SOURCES))
USER_ELFS = $(USER_EASM_ELFS) $(USER_C_ELFS)
USER_SDK_SOURCES = $(wildcard ./user/sdk/src/*.c)
USER_SDK_OBJECTS = $(patsubst ./user/sdk/src/%.c,./build/user_sdk_%.o,$(USER_SDK_SOURCES))
USER_SDK_LIB = ./build/libos64.a
USER_SDK_HEADERS = $(wildcard ./user/sdk/include/os64/*.h) $(wildcard ./user/sdk/src/*.h) ./include/os64/display_types.h ./include/os64/input_types.h ./include/os64/ipc_types.h ./include/os64/process_types.h ./include/os64/service_types.h ./include/os64/service_manager_types.h ./include/os64/service_protocol_types.h ./include/os64/surface_types.h ./include/os64/thread_types.h ./include/os64/window_types.h
WINDOWD_MODULE_SOURCES = $(wildcard ./user/programs/windowd/*.c)
WINDOWD_MODULE_OBJECTS = $(patsubst ./user/programs/windowd/%.c,./build/windowd_%.o,$(WINDOWD_MODULE_SOURCES))
WINDOW_DEMO_OBJECT = ./build/window_demo.o


# Policy-driven driver build inputs
DRIVER_POLICY_CONFIG = ./config/drivers.json
DRIVER_POLICY_SETTINGS = $(addprefix ./drivers/,$(addsuffix /settings.json,$(shell python3 -c 'import json; print(" ".join(x["path"] for x in json.load(open("config/drivers.json"))["drivers"]))')))
DRIVER_POLICY_MK = ./build/generated/driver_policy.mk
GENERATED_LINKED_DRIVER_REGISTRY = ./build/generated/linked_driver_registry.cpp
GENERATED_DRIVER_ACTIVATION = ./build/generated/driver_activation.cpp
DRIVER_POLICY_GENERATOR = ./tools/gen_driver_build.py

$(DRIVER_POLICY_MK) $(GENERATED_LINKED_DRIVER_REGISTRY) $(GENERATED_DRIVER_ACTIVATION) &: $(DRIVER_POLICY_CONFIG) $(DRIVER_POLICY_SETTINGS) $(DRIVER_POLICY_GENERATOR) ./tools/driver_policy.py
	@mkdir -p ./build/generated
	python3 $(DRIVER_POLICY_GENERATOR) --make-output $(DRIVER_POLICY_MK) --registry-output $(GENERATED_LINKED_DRIVER_REGISTRY) --activation-output $(GENERATED_DRIVER_ACTIVATION)

include $(DRIVER_POLICY_MK)

DRIVER_ABI_FIXTURES = ./bin/hello.drv ./bin/provider.drv ./bin/consumer.drv
ROOT_DRIVER_PACKAGES = $(DRIVER_PACKAGES) $(DRIVER_ABI_FIXTURES)
USER_EXTRA_ARGS = $(foreach file,$(USER_BINS) $(USER_ELFS) $(ROOT_DRIVER_PACKAGES),--extra-file-auto $(file))

.PHONY: all all64 uefi uefi-diagnostic drivers driver-projects test-user-sdk test-phase1 test-shutdown test-graphics test-graphics-contracts test-graphics-demo test-gop-present test-display-contracts test-display-present test-display-handoff test-drive-free-scheduler test-window-contracts test-window-single test-window-multi-contracts test-window-multi test-window-input-contracts test-window-input test-window-sdk-contracts test-window-sdk test-gui-app test-gui-recovery test-gui-soak test-graphics-clip test-surface-backing test-surface-backing-contracts test-surface-backing-smoke test-surface-abi test-input test-input-queue test-input-event-loop test-ipc-contracts test-ipc-smoke test-ipc test-kernel-handles test-process-lifecycle test-thread-model test-thread-main test-thread-abi test-thread-waits test-thread-sync test-thread-readiness test-thread-faults test-thread-soak test-phase45-abc test-phase45 test-service-registry test-service-smoke test-service-manager-smoke test-service-supervision test-first-services test-services test-spinlocks test-concurrency test-fault-injection test-soak test-soak-hour test-abi-freeze test-driver-policy test-driver-layout test-driver-build test-driver-boot test-driver-regression test-driver-ownership test-driver-va test-driver-image-memory test-driver-alloc test-driver-context test-driver-mmio test-pci-mmio-va test-dma-coherent test-dma-streaming test-dma-domain test-driver-quiesce test-driver-dma-device test-driver-memory-faults test-driver-memory-soak test-phase47 test-phase4-entry test-phase4 test-uefi-smoke test-uefi-userland test-uefi-screen test-cpu-topology test-smp-topology test-percpu test-smp-emergency-entry test-ap-startup-state test-ap-bringup test-phase46-foundation test-smp-scheduler test-smp-timer test-smp-preemption test-smp-remote-wake test-smp-ipi test-smp-affinity test-smp-execution test-tlb-shootdown test-tlb-lock-order test-smp-memory test-closure clean

KERNEL64_OBJECTS = \
	./build/kernel64_entry.o \
	./build/ap_trampoline_blob.o \
	./build/kernel64.o \
	./build/fault_injection64.o \
	./build/spinlock64.o \
	./build/thread_sync64.o \
	./build/kutil64.o \
	./build/kernel_diag64.o \
	./build/ipc_mailbox64.o \
	./build/ipc64.o \
	./build/service_registry64.o \
	./build/kernel_handle64.o \
	./build/kernel_objects64.o \
	./build/process64.o \
	./build/process_surface64.o \
	./build/userprog64.o \
	./build/syscall64.o \
	./build/vfs_syscalls64.o \
	./build/sdk_syscalls64.o \
	./build/klog64.o \
	./build/panic64.o \
	./build/acpi64.o \
	./build/madt_cpu64.o \
	./build/acpi_power64.o \
	./build/cpu64.o \
	./build/cpu_local64.o \
	./build/smp64.o \
	./build/apic64.o \
	./build/ksh64.o \
	./build/driver_manager64.o \
	./build/driver_resource64.o \
	./build/driver_va64.o \
	./build/driver_alloc64.o \
	./build/driver_mmio64.o \
	./build/driver_dma64.o \
	./build/driver_exports64.o \
	./build/driver_binding64.o \
	./build/driver_irq64.o \
	./build/driver_loader64.o \
	./build/driver_unload64.o \
	./build/driver_builtin64.o \
	./build/driver_activation64.o \
	./build/driver_shell64.o \
	./build/kernel_exports64.o \
	./build/pci64.o \
	./build/input_event_queue64.o \
	./build/input_events64.o \
	./build/graphics_clip64.o \
	./build/graphics_surface64.o \
	./build/surface_backing64.o \
	./build/graphics_draw64.o \
	./build/graphics_dirty64.o \
	./build/graphics_present64.o \
	./build/graphics_font64.o \
	./build/display_backend64.o \
	./build/display_owner64.o \
	$(LINKED_DRIVER_OBJECTS) \
	./build/vfs64.o \
	./build/idt64.o \
	./build/idt64_asm.o \
	./build/gdt64.o \
	./build/gdt64_asm.o \
	./build/user64_asm.o \
	./build/pmm.o \
	./build/vm.o \
	./build/address_space.o \
	./build/paging_x86_64.o \
	./build/heap.o

KERNEL_PUBLIC_HEADERS = $(shell find ./include -type f)
$(KERNEL64_OBJECTS): $(KERNEL_PUBLIC_HEADERS)

all: all64
all64: driver-projects ./bin/os64.bin
uefi: driver-projects ./bin/uefi_esp.img ./bin/OVMF_VARS_4M.fd
uefi-diagnostic: driver-projects ./bin/uefi_diag_esp.img ./bin/OVMF_VARS_4M.fd
drivers: driver-projects
driver-projects: $(DRIVER_POLICY_MK) $(DRIVER_PACKAGES) $(LINKED_DRIVER_OBJECTS)
	@set -e; for dir in $(ENABLED_DRIVER_DIRS); do $(MAKE) -s -C $$dir info >/dev/null; done
test-user-sdk: uefi
	bash ./tools/run_usdk_test.sh
test-cpu-topology:
	python3 ./tools/cpu_topology_test.py
test-smp-topology: uefi-diagnostic
	python3 ./tools/smp_topology_smoke.py
test-percpu:
	python3 ./tools/percpu_test.py
test-smp-emergency-entry: uefi-diagnostic
	python3 ./tools/smp_emergency_entry_smoke.py
test-ap-startup-state:
	python3 ./tools/ap_startup_state_test.py
test-ap-bringup: uefi-diagnostic test-ap-startup-state
	python3 ./tools/ap_bringup_smoke.py
test-phase46-foundation: test-cpu-topology test-percpu test-spinlocks test-smp-topology test-smp-emergency-entry
test-smp-scheduler: test-thread-model
test-smp-timer: test-smp-topology
test-smp-preemption: uefi-diagnostic
	python3 ./tools/smp_execution_smoke.py
test-smp-remote-wake: uefi-diagnostic
	python3 ./tools/smp_remote_wake_smoke.py
test-smp-ipi: test-smp-remote-wake
test-smp-affinity: test-thread-model test-smp-preemption test-smp-remote-wake
test-smp-execution: test-smp-scheduler test-smp-timer test-smp-preemption test-smp-ipi test-smp-affinity
test-tlb-lock-order:
	python3 ./tools/tlb_lock_order_test.py
test-tlb-shootdown: test-tlb-lock-order uefi-diagnostic
	python3 ./tools/tlb_shootdown_smoke.py
test-smp-memory: test-tlb-shootdown test-surface-abi test-thread-waits
.PHONY: test-smp-spinlocks test-smp-concurrency test-smp-interrupt-ownership test-smp-services-gui test-phase46-audit test-smp-faults test-smp-soak test-phase46 test-phase46-closure
test-smp-spinlocks: test-spinlocks test-tlb-lock-order
test-smp-concurrency: test-concurrency test-smp-memory
test-smp-interrupt-ownership: uefi-diagnostic
	python3 ./tools/smp_irq_ownership_smoke.py
test-smp-services-gui: uefi
	OS64_QEMU_CPUS=4 python3 ./tools/service_manager_smoke.py
	OS64_QEMU_CPUS=4 python3 ./tools/gui_recovery_smoke.py
	OS64_QEMU_CPUS=4 python3 ./tools/input_event_loop_smoke.py
test-phase46-audit: test-smp-spinlocks test-smp-concurrency test-smp-interrupt-ownership test-smp-services-gui
test-smp-faults: uefi uefi-diagnostic test-fault-injection test-ap-startup-state test-smp-emergency-entry test-tlb-lock-order
	python3 ./tools/smp_fault_injection_test.py
	OS64_QEMU_CPUS=4 python3 ./tools/thread_fault_injection_smoke.py
test-smp-soak: uefi
	python3 ./tools/thread_soak.py --duration 60 --cpus 4
test-phase46: test-phase45 test-phase46-foundation test-smp-execution test-smp-memory test-phase46-audit test-smp-faults test-smp-soak
test-phase46-closure: test-phase46 test-closure
test-phase1: uefi uefi-diagnostic
	python3 ./tools/phase1_smoke.py
test-shutdown: uefi
	python3 ./tools/acpi_shutdown_smoke.py
test-graphics-contracts: test-surface-backing-contracts
	python3 ./tools/graphics_clip_test.py
	python3 ./tools/graphics_surface_test.py
	python3 ./tools/graphics_draw_test.py
	python3 ./tools/graphics_dirty_test.py
	python3 ./tools/graphics_dirty_present_test.py
	python3 ./tools/graphics_font_test.py
test-graphics: test-graphics-contracts test-surface-backing-smoke test-graphics-demo test-gop-present test-display-present test-window-single test-window-multi test-window-sdk
test-graphics-clip: test-graphics-contracts
test-surface-backing: test-surface-backing-contracts test-surface-backing-smoke
test-surface-backing-contracts:
	python3 ./tools/surface_backing_test.py
test-surface-backing-smoke: uefi
	python3 ./tools/surface_backing_smoke.py
test-surface-abi: uefi
	python3 ./tools/surface_mapping_test.py
	python3 ./tools/ipc_core_test.py
	python3 ./tools/surface_mapping_smoke.py
test-graphics-demo: uefi
	python3 ./tools/graphics_demo_smoke.py
test-gop-present: uefi
	python3 ./tools/gop_present_smoke.py
test-display-contracts:
	python3 ./tools/display_protocol_test.py
	python3 ./tools/display_backend_test.py
test-display-present: test-display-contracts uefi
	python3 ./tools/display_present_smoke.py
test-display-handoff: test-abi-freeze test-window-multi-contracts
	python3 ./tools/display_handoff_test.py
test-drive-free-scheduler: uefi
	python3 ./tools/scheduler_idle_test.py
	python3 ./tools/drive_free_scheduler_smoke.py
test-window-contracts:
	python3 ./tools/window_single_test.py
test-window-single: test-window-contracts uefi
	python3 ./tools/window_single_smoke.py
test-window-multi-contracts:
	python3 ./tools/window_multi_test.py
test-window-multi: test-window-multi-contracts uefi
	python3 ./tools/window_multi_smoke.py
test-window-input-contracts:
	python3 ./tools/window_input_test.py
test-window-input: test-window-input-contracts uefi
	python3 ./tools/window_input_smoke.py
test-window-sdk-contracts:
	python3 ./tools/window_sdk_test.py
test-gui-app: uefi
	python3 ./tools/gui_app_smoke.py
test-gui-recovery: uefi
	python3 ./tools/gui_recovery_smoke.py
test-gui-soak: uefi
	python3 ./tools/gui_soak.py --duration 60
test-window-sdk: test-window-sdk-contracts test-gui-app
test-input-queue:
	python3 ./tools/input_event_queue_test.py
	python3 ./tools/keyboard_event_translation_test.py
	python3 ./tools/process_event_queue_test.py
test-input-event-loop: uefi
	python3 ./tools/input_event_loop_smoke.py
test-input: test-input-queue test-input-event-loop
test-ipc-contracts:
	python3 ./tools/ipc_mailbox_test.py
	python3 ./tools/ipc_core_test.py
test-ipc-smoke: uefi
	python3 ./tools/ipc_smoke.py
test-ipc: test-ipc-contracts test-ipc-smoke

test-kernel-handles:
	python3 ./tools/kernel_handle_test.py

test-process-lifecycle:
	python3 ./tools/process_lifecycle_test.py

test-thread-model:
	python3 ./tools/thread_model_test.py

test-thread-main: test-thread-model test-process-lifecycle
	python3 ./tools/thread_abi_test.py

test-thread-abi: test-thread-main uefi
	python3 ./tools/thread_smoke.py

test-thread-waits: test-thread-abi
	python3 ./tools/thread_wait_test.py

test-thread-sync: uefi test-user-sdk
	python3 ./tools/thread_sync_smoke.py

test-thread-readiness: uefi test-user-sdk
	python3 ./tools/thread_readiness_smoke.py

test-thread-faults: uefi uefi-diagnostic test-fault-injection
	python3 ./tools/thread_fault_injection_smoke.py

test-thread-soak: uefi
	python3 ./tools/thread_soak.py --duration 60

test-phase45-abc: test-thread-abi test-user-sdk test-drive-free-scheduler

test-phase45: test-phase45-abc test-thread-waits test-thread-sync test-thread-readiness test-thread-faults test-thread-soak

test-spinlocks:
	python3 ./tools/spinlock_test.py

test-concurrency: test-spinlocks test-kernel-handles test-process-lifecycle test-service-registry test-ipc-contracts

test-fault-injection: uefi uefi-diagnostic test-concurrency
	python3 ./tools/fault_injection_test.py
	python3 ./tools/fault_injection_smoke.py

test-soak: uefi
	python3 ./tools/service_soak.py --duration 60

test-soak-hour: uefi
	python3 ./tools/service_soak.py --duration 3600

test-abi-freeze:
	python3 ./tools/abi_freeze_test.py

test-driver-policy:
	python3 ./tools/driver_policy.py
	python3 ./tools/driver_policy_test.py
test-driver-ownership:
	python3 ./tools/driver_ownership_test.py

test-driver-va:
	python3 ./tools/driver_va_test.py

test-driver-image-memory:
	python3 ./tools/driver_image_memory_test.py

test-driver-alloc:
	python3 ./tools/driver_alloc_test.py

test-driver-context:
	python3 ./tools/driver_context_test.py

test-driver-mmio:
	python3 ./tools/driver_mmio_test.py

test-pci-mmio-va:
	python3 ./tools/driver_mmio_test.py

test-dma-coherent:
	python3 ./tools/dma_coherent_test.py

test-dma-streaming:
	python3 ./tools/dma_streaming_test.py

test-dma-domain:
	python3 ./tools/dma_coherent_test.py

test-driver-quiesce:
	python3 ./tools/driver_quiesce_test.py

test-driver-dma-device: uefi
	python3 ./tools/driver_dma_device_smoke.py

test-driver-memory-faults: uefi-diagnostic test-driver-va test-driver-image-memory test-driver-alloc test-driver-mmio test-dma-coherent test-dma-streaming test-driver-quiesce
	python3 ./tools/driver_memory_fault_test.py
	python3 ./tools/driver_fault_injection_smoke.py

test-driver-memory-soak: uefi
	python3 ./tools/driver_memory_soak.py --duration 60

test-phase47: test-driver-ownership test-driver-va test-driver-image-memory test-driver-alloc test-driver-context test-driver-mmio test-pci-mmio-va test-dma-coherent test-dma-streaming test-dma-domain test-driver-quiesce test-driver-memory-faults test-driver-dma-device test-driver-memory-soak
	@echo "Phase 4.7 driver memory and DMA aggregate OK"

test-driver-layout: test-driver-policy
	python3 ./tools/driver_layout_test.py

test-driver-build: test-driver-layout
	python3 ./tools/driver_build_integration_test.py

test-driver-boot:
	python3 ./tools/boot_driver_handoff_smoke.py

test-driver-regression: test-driver-build test-driver-boot test-uefi-smoke test-user-sdk
	python3 ./tools/driver_regression_matrix_test.py

test-phase4-entry: test-driver-regression test-graphics-contracts test-concurrency
	python3 ./tools/phase4_entry_test.py

test-phase4: test-surface-backing test-surface-abi test-display-present test-display-handoff test-drive-free-scheduler test-window-single test-window-multi test-window-input test-window-sdk test-gui-recovery test-fault-injection test-gui-soak

test-uefi-smoke: uefi
	python3 ./tools/uefi_smoke.py

test-uefi-userland: uefi
	python3 ./tools/uefi_userland_smoke.py

test-uefi-screen: uefi
	python3 ./tools/uefi_screen_smoke.py

test-closure: test-abi-freeze test-phase4-entry test-phase4 test-phase1 test-shutdown test-uefi-smoke test-uefi-userland test-uefi-screen test-user-sdk test-graphics test-input test-ipc test-services test-concurrency test-soak

test-service-registry:
	python3 ./tools/service_registry_test.py
test-service-smoke: uefi
	python3 ./tools/service_registry_smoke.py
test-service-manager-smoke: uefi
	python3 ./tools/service_manager_smoke.py
test-service-supervision: test-service-registry test-service-manager-smoke
test-first-services: uefi
	python3 ./tools/first_services_smoke.py
test-services: test-service-registry test-service-smoke test-service-supervision test-first-services

all32:
	@echo "legacy BIOS build is archived under archive/legacy-bios and is not part of the active build."
	@exit 1

./bin/os64.bin: ./bin/kernel64.bin $(USER_BINS) $(USER_ELFS) $(ROOT_DRIVER_PACKAGES) ./tools/build_fat32_root_image.py
	python3 ./tools/build_fat32_root_image.py \
		--kernel ./bin/kernel64.bin \
		$(USER_EXTRA_ARGS) \
		--output ./bin/os64.bin

./build/kernel64_entry.o: ./arch/x86_64/kernel64_entry.asm
	@mkdir -p ./build
	$(AS) -f elf64 -g $< -o $@

./build/kernel64.o: ./kernel/core/kernel64.cpp ./kernel/core/kernel64_main.cpp ./kernel/core/kernel64_process.cpp ./kernel/core/kernel64_diag.cpp ./kernel/core/kernel64_user.cpp ./kernel/core/kernel64_irq.cpp ./include/drivers/gop.h ./include/drivers/keyboard.h ./include/drivers/pit.h ./include/kernel/handle/kernel_objects.h ./include/kernel/process.h ./include/kernel/process64.h ./include/kernel/syscall64.h
	@mkdir -p ./build
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/core/kernel64.cpp -o $@

./build/spinlock64.o: ./kernel/sync/spinlock.cpp ./include/kernel/spinlock.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/thread_sync64.o: ./kernel/sync/thread_sync.cpp ./include/kernel/sync/thread_sync.h ./include/kernel/process64.h ./include/kernel/handle/kernel_handle.h ./include/os64/sync_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/fault_injection64.o: ./kernel/debug/fault_injection.cpp ./include/kernel/fault_injection.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kutil64.o: ./kernel/util/kutil64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kernel_diag64.o: ./kernel/process/kernel_diag.cpp ./include/kernel/kernel_diag.h ./include/kernel/process.h ./include/kernel/handle/kernel_objects.h ./include/kernel/graphics/surface_backing.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/ipc_mailbox64.o: ./kernel/ipc/ipc_mailbox.cpp ./include/kernel/ipc/ipc_mailbox.h ./include/os64/ipc_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/ipc64.o: ./kernel/ipc/ipc.cpp ./include/kernel/handle/kernel_objects.h ./include/kernel/ipc/ipc.h ./include/kernel/ipc/ipc_mailbox.h ./include/kernel/process.h ./include/kernel/process64.h ./include/os64/ipc_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/service_registry64.o: ./kernel/service/service_registry.cpp ./include/kernel/service/service_registry.h ./include/kernel/process.h ./include/kernel/process64.h ./include/os64/service_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kernel_handle64.o: ./kernel/handle/kernel_handle.cpp ./include/kernel/handle/kernel_handle.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kernel_objects64.o: ./kernel/handle/kernel_objects.cpp ./include/kernel/handle/kernel_objects.h ./include/kernel/handle/kernel_handle.h ./include/kernel/graphics/graphics2d.h ./include/kernel/graphics/surface_backing.h ./include/kernel/mm/vm.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/process64.o: ./kernel/process/process64.cpp ./include/kernel/process64.h ./include/kernel/process.h ./include/kernel/thread.h ./include/os64/thread_types.h ./include/kernel/handle/kernel_handle.h ./include/kernel/handle/kernel_objects.h ./include/kernel/input/input_event_queue.h ./include/kernel/ipc/ipc_mailbox.h ./include/kernel/service/service_registry.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/process_surface64.o: ./kernel/process/process_surface.cpp ./include/kernel/process_surface.h ./include/kernel/process.h ./include/kernel/thread.h ./include/kernel/handle/kernel_objects.h ./include/kernel/graphics/surface_backing.h ./include/kernel/mm/address_space.h ./include/os64/surface_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/userprog64.o: ./kernel/process/userprog64.cpp ./include/kernel/process.h ./include/kernel/process64.h ./include/kernel/thread.h ./include/kernel/userprog64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/syscall64.o: ./kernel/syscall/syscall64.cpp ./kernel/syscall/sdk_syscalls.h ./kernel/syscall/vfs_syscalls.h ./include/drivers/keyboard.h ./include/fs/vfs.h ./include/kernel/kernel_diag.h ./include/kernel/process.h ./include/kernel/process64.h ./include/kernel/thread.h ./include/kernel/syscall64.h ./include/kernel/userprog64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/vfs_syscalls64.o: ./kernel/syscall/vfs_syscalls.cpp ./kernel/syscall/vfs_syscalls.h ./include/fs/vfs.h ./include/kernel/handle/kernel_handle.h ./include/kernel/process64.h ./include/kernel/syscall64.h ./include/kernel/userprog64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/sdk_syscalls64.o: ./kernel/syscall/sdk_syscalls.cpp ./kernel/syscall/sdk_syscalls.h ./include/drivers/gop.h ./include/drivers/keyboard.h ./include/drivers/pit.h ./include/kernel/input/input_events.h ./include/kernel/ipc/ipc.h ./include/kernel/process.h ./include/kernel/process64.h ./include/kernel/thread.h ./include/kernel/service/service_registry.h ./include/kernel/syscall64.h ./include/kernel/userprog64.h ./include/os64/graphics_types.h ./include/os64/input_types.h ./include/os64/ipc_types.h ./include/os64/service_types.h ./include/os64/thread_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/klog64.o: ./kernel/log/klog.cpp ./include/kernel/klog.h ./include/kernel/kutil64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/panic64.o: ./kernel/panic/panic.cpp ./include/kernel/panic.h ./include/kernel/klog.h ./include/kernel/boot_info.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/acpi64.o: ./kernel/acpi/acpi.cpp ./include/kernel/acpi.h ./include/kernel/klog.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/madt_cpu64.o: ./kernel/acpi/madt_cpu.cpp ./include/kernel/acpi.h ./include/kernel/acpi_madt.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/acpi_power64.o: ./kernel/acpi/acpi_power.cpp ./include/kernel/acpi.h ./include/arch/x86_64/io.h ./include/kernel/mm/vm.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/cpu64.o: ./kernel/cpu/cpu.cpp ./include/kernel/cpu.h ./include/kernel/acpi.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/cpu_local64.o: ./kernel/cpu/cpu_local.cpp ./include/kernel/cpu_local.h ./include/kernel/cpu.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/smp64.o: ./kernel/cpu/smp.cpp ./include/kernel/smp.h ./include/kernel/cpu.h ./include/kernel/cpu_local.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./bin/ap_trampoline.bin: ./arch/x86_64/ap_trampoline.asm
	$(AS) -f bin $< -o $@

./build/ap_trampoline_blob.o: ./bin/ap_trampoline.bin ./arch/x86_64/ap_trampoline_blob.asm
	$(AS) -f elf64 ./arch/x86_64/ap_trampoline_blob.asm -o $@

./build/apic64.o: ./arch/x86_64/apic.cpp ./include/arch/x86_64/apic.h ./include/kernel/acpi.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/ksh64.o: ./kernel/shell/ksh64.cpp ./include/kernel/pci.h ./include/drivers/gop.h ./include/kernel/kernel_diag.h ./include/kernel/process64.h ./include/kernel/handle/kernel_objects.h ./include/kernel/graphics/surface_backing.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/shell/ksh64.cpp -o $@

./build/driver_manager64.o: ./kernel/driver/driver_manager.cpp ./include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_resource64.o: ./kernel/driver/driver_resource.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/spinlock.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_va64.o: ./kernel/driver/driver_va.cpp ./include/kernel/driver/driver_va.h ./include/kernel/driver/driver_manager.h ./include/kernel/spinlock.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_alloc64.o: ./kernel/driver/driver_alloc.cpp ./include/kernel/driver/driver_alloc.h ./include/kernel/driver/driver_manager.h ./include/kernel/spinlock.h ./include/kernel/mm/vm.h ./include/kernel/fault_injection.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_mmio64.o: ./kernel/driver/driver_mmio.cpp ./include/kernel/driver/driver_mmio.h ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_alloc.h ./include/kernel/mm/vm.h ./include/kernel/pci.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_dma64.o: ./kernel/driver/driver_dma.cpp ./include/kernel/driver/driver_dma.h ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_alloc.h ./include/kernel/mm/pmm.h ./include/kernel/mm/vm.h ./include/kernel/pci.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_exports64.o: ./kernel/driver/driver_exports.cpp ./include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_binding64.o: ./kernel/driver/driver_binding.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_alloc.h ./include/kernel/pci.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_irq64.o: ./kernel/driver/driver_irq.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_alloc.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_loader64.o: ./kernel/driver/driver_loader.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_va.h ./include/kernel/driver/driver_alloc.h ./include/kernel/driver/drv_format.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_unload64.o: ./kernel/driver/driver_unload.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_va.h ./include/kernel/driver/driver_alloc.h ./include/kernel/driver/driver_mmio.h ./include/kernel/driver/drv_format.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_builtin64.o: $(GENERATED_LINKED_DRIVER_REGISTRY) ./include/kernel/driver/driver_manager.h ./include/kernel/driver/drv_format.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_activation64.o: $(GENERATED_DRIVER_ACTIVATION) ./include/kernel/driver/driver_manager.h ./include/kernel/boot_info.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_shell64.o: ./kernel/driver/driver_shell.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_va.h ./include/kernel/driver/driver_alloc.h ./include/kernel/driver/driver_mmio.h ./include/kernel/driver/driver_dma.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kernel_exports64.o: ./kernel/driver/kernel_exports.cpp ./include/kernel/driver/kernel_exports.h ./include/kernel/driver/driver_manager.h ./include/kernel/driver/driver_mmio.h ./include/kernel/driver/driver_dma.h ./include/kernel/pci.h ./include/arch/x86_64/io.h ./include/drivers/ata.h ./include/drivers/gop.h ./include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/driver/kernel_exports.cpp -o $@

./build/pci64.o: ./kernel/pci/pci.cpp ./include/kernel/pci.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/input_event_queue64.o: ./kernel/input/input_event_queue.cpp ./include/kernel/input/input_event_queue.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/input_events64.o: ./kernel/input/input_events.cpp ./include/kernel/input/input_events.h ./include/kernel/input/input_event_queue.h ./include/kernel/process64.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_clip64.o: ./kernel/graphics/graphics_clip.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_surface64.o: ./kernel/graphics/graphics_surface.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/surface_backing64.o: ./kernel/graphics/surface_backing.cpp ./include/kernel/graphics/surface_backing.h ./include/kernel/mm/pmm.h ./include/kernel/mm/vm.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_draw64.o: ./kernel/graphics/graphics_draw.cpp ./include/kernel/graphics/graphics2d.h ./include/kernel/graphics/graphics_font.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_dirty64.o: ./kernel/graphics/graphics_dirty.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_present64.o: ./kernel/graphics/graphics_present.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_font64.o: ./kernel/graphics/graphics_font.cpp ./include/kernel/graphics/graphics_font.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/display_backend64.o: ./kernel/graphics/display_backend.cpp ./include/kernel/graphics/display_backend.h ./include/drivers/gop.h ./include/kernel/graphics/graphics2d.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/display_owner64.o: ./kernel/graphics/display_owner.cpp ./include/kernel/graphics/display_owner.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/terminal64.o: ./drivers/display/terminal/terminal.cpp ./include/kernel/graphics/graphics_font.h ./include/kernel/graphics/display_owner.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/gop64.o: ./drivers/display/gop/gop.cpp ./include/drivers/gop.h ./include/kernel/boot_info.h ./include/kernel/graphics/graphics2d.h ./include/kernel/graphics/display_owner.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/ata64.o: ./drivers/block/ata/ata.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/keyboard64.o: ./drivers/input/ps2_keyboard/keyboard.cpp ./include/drivers/keyboard.h ./include/kernel/input/input_events.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/pit64.o: ./drivers/timer/pit/pit.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/fat32_64.o: ./drivers/fs/fat32/src/fat32.cpp ./drivers/fs/fat32/src/fat32_common.cpp ./drivers/fs/fat32/src/fat32_dir.cpp ./drivers/fs/fat32/src/fat32_lfn.cpp ./drivers/fs/fat32/src/fat32_cluster.cpp ./drivers/fs/fat32/src/fat32_api.cpp ./drivers/fs/fat32/include/fat32.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./drivers/fs/fat32/src/fat32.cpp -o $@

./build/fat32_vfs64.o: ./drivers/fs/fat32/src/fat32_vfs.cpp ./drivers/fs/fat32/include/fat32.h ./include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/vfs64.o: ./kernel/vfs/vfs.cpp ./kernel/vfs/vfs_common.cpp ./kernel/vfs/vfs_memfs.cpp ./kernel/vfs/vfs_core.cpp ./kernel/vfs/vfs_open.cpp ./include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/vfs/vfs.cpp -o $@

./build/idt64.o: ./arch/x86_64/idt64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/idt64_asm.o: ./arch/x86_64/idt64.asm
	$(AS) -f elf64 -g $< -o $@

./build/gdt64.o: ./arch/x86_64/gdt64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/gdt64_asm.o: ./arch/x86_64/gdt64.asm
	$(AS) -f elf64 -g $< -o $@

./build/user64_asm.o: ./arch/x86_64/user64.asm
	$(AS) -f elf64 -g $< -o $@

./build/pmm.o: ./kernel/mm/pmm.cpp ./include/kernel/mm/pmm.h ./include/kernel/boot_info.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/vm.o: ./kernel/mm/vm.cpp ./include/kernel/mm/vm.h ./include/kernel/mm/pmm.h ./include/kernel/mm/arch_vm.h ./include/kernel/smp.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/address_space.o: ./kernel/mm/address_space.cpp ./include/kernel/mm/address_space.h ./include/kernel/mm/vm.h ./include/kernel/mm/pmm.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/paging_x86_64.o: ./arch/x86_64/mm/paging.cpp ./include/kernel/mm/arch_vm.h ./include/kernel/mm/vm.h ./include/kernel/mm/pmm.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/heap.o: ./kernel/mm/heap.cpp ./include/kernel/mm/heap.h ./include/kernel/mm/vm.h ./include/kernel/mm/pmm.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./bin/kernel64.elf: $(KERNEL64_OBJECTS)
	$(HOST64_LD) -m elf_x86_64 -nostdlib -T ./arch/x86_64/linkerScript64.ld -o $@ $(KERNEL64_OBJECTS)

./bin/kernel64.bin: ./bin/kernel64.elf
	@mkdir -p ./bin
	$(HOST64_OBJCOPY) -O binary $< $@

./build/uefi_boot.o: ./boot/uefi/uefi_boot.c ./include/kernel/boot_info.h
	@mkdir -p ./build
	$(UEFI_CC) $(UEFI_CFLAGS) -c $< -o $@

./build/uefi_entry.o: ./boot/uefi/uefi_entry.asm
	@mkdir -p ./build
	$(AS) -f elf64 -g $< -o $@

./build/BOOTX64.elf: ./build/uefi_entry.o ./build/uefi_boot.o ./boot/uefi/uefi.ld
	$(UEFI_LD) -m elf_x86_64 -nostdlib -T ./boot/uefi/uefi.ld -o $@ ./build/uefi_entry.o ./build/uefi_boot.o

./bin/BOOTX64.EFI: ./build/BOOTX64.elf
	@mkdir -p ./bin
	$(UEFI_OBJCOPY) -j .text -j .rodata -j .data -j .bss -O pei-x86-64 --subsystem efi-app --image-base 0x400000 --stack 0x100000,0x1000 $< $@

./bin/uefi_esp.img: ./bin/BOOTX64.EFI ./bin/kernel64.bin ./bin/os64.bin $(BOOT_DRIVER_PACKAGES) ./tools/build_uefi_esp.py
	python3 ./tools/build_uefi_esp.py --efi ./bin/BOOTX64.EFI --kernel ./bin/kernel64.bin --root ./bin/os64.bin $(foreach file,$(BOOT_DRIVER_PACKAGES),--boot-driver $(file)) --output ./bin/uefi_esp.img

./bin/uefi_diag_esp.img: ./bin/BOOTX64.EFI ./bin/kernel64.bin ./bin/os64.bin $(BOOT_DRIVER_PACKAGES) ./tools/build_uefi_esp.py
	python3 ./tools/build_uefi_esp.py --efi ./bin/BOOTX64.EFI --kernel ./bin/kernel64.bin --root ./bin/os64.bin $(foreach file,$(BOOT_DRIVER_PACKAGES),--boot-driver $(file)) --diagnostic --output ./bin/uefi_diag_esp.img

./bin/OVMF_VARS_4M.fd:
	@mkdir -p ./bin
	cp $(OVMF_VARS_TEMPLATE) ./bin/OVMF_VARS_4M.fd

./bin/fat32.img: ./tools/build_fat32_image.py
	@mkdir -p ./bin
	python3 ./tools/build_fat32_image.py --output ./bin/fat32.img

./bin/%.bin: ./user/programs/%.asm
	@mkdir -p ./bin
	$(AS) -f bin -o $@ $<

./build/user_elf_%.o: ./user/programs/%.easm
	@mkdir -p ./build
	$(AS) -f elf64 -g -o $@ $<

./build/user_c_%.o: ./user/programs/%.c $(USER_SDK_HEADERS)
	@mkdir -p ./build
	$(HOST64_CC) $(USER64_CFLAGS) -c $< -o $@

./build/windowd_%.o: ./user/programs/windowd/%.c $(USER_SDK_HEADERS) $(wildcard ./user/programs/windowd/*.h)
	@mkdir -p ./build
	$(HOST64_CC) $(USER64_CFLAGS) -c $< -o $@

$(WINDOW_DEMO_OBJECT): ./user/programs/windowdemo/window_demo.c $(USER_SDK_HEADERS)
	@mkdir -p ./build
	$(HOST64_CC) $(USER64_CFLAGS) -c $< -o $@

./build/user_sdk_%.o: ./user/sdk/src/%.c $(USER_SDK_HEADERS)
	@mkdir -p ./build
	$(HOST64_CC) $(USER64_CFLAGS) -Os -c $< -o $@

$(USER_SDK_LIB): $(USER_SDK_OBJECTS)
	@mkdir -p ./build
	$(HOST64_AR) rcs $@ $^

./build/user_crt0.o: ./user/programs/user_crt0.easm
	@mkdir -p ./build
	$(AS) -f elf64 -g -o $@ $<

$(USER_EASM_ELFS): ./bin/%.elf: ./build/user_elf_%.o ./user/programs/user_elf.ld
	@mkdir -p ./bin
	$(HOST64_LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T ./user/programs/user_elf.ld -o $@ $<

$(USER_C_ELFS): ./bin/%.elf: ./build/user_c_%.o ./build/user_crt0.o $(USER_SDK_LIB) ./user/programs/user_elf.ld
	@mkdir -p ./bin
	$(HOST64_LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T ./user/programs/user_elf.ld -o $@ ./build/user_crt0.o $< $(USER_PROGRAM_EXTRA_OBJECTS) $(USER_SDK_LIB)

./bin/windowd_c.elf: USER_PROGRAM_EXTRA_OBJECTS = $(WINDOWD_MODULE_OBJECTS)
./bin/windowd_c.elf: $(WINDOWD_MODULE_OBJECTS)
./bin/usdk_c.elf: USER_PROGRAM_EXTRA_OBJECTS = $(WINDOW_DEMO_OBJECT)
./bin/usdk_c.elf: $(WINDOW_DEMO_OBJECT)

./build/user_c_ushell_c.o: ./user/programs/ushell/ushell_helpers.inc ./user/programs/ushell/ushell_main.inc ./user/include/userlib.h ./user/include/userlib/userlib_syscalls.h ./user/include/userlib/userlib_text.h ./user/include/userlib/userlib_path_input.h

./build/hello.unsigned.drv: ./tools/build_hello_drv.py
	@mkdir -p ./build
	python3 ./tools/build_hello_drv.py --output $@

./bin/hello.drv: ./build/hello.unsigned.drv ./tools/driver_builder/sign_drv.py
	@mkdir -p ./bin
	python3 ./tools/driver_builder/sign_drv.py --input $< --output $@ --algorithm local-test

./build/provider.unsigned.drv ./build/consumer.unsigned.drv: ./tools/build_driver_samples.py
	@mkdir -p ./build
	python3 ./tools/build_driver_samples.py --provider ./build/provider.unsigned.drv --consumer ./build/consumer.unsigned.drv

./bin/provider.drv: ./build/provider.unsigned.drv ./tools/driver_builder/sign_drv.py
	@mkdir -p ./bin
	python3 ./tools/driver_builder/sign_drv.py --input $< --output $@ --algorithm local-test

./bin/consumer.drv: ./build/consumer.unsigned.drv ./tools/driver_builder/sign_drv.py
	@mkdir -p ./bin
	python3 ./tools/driver_builder/sign_drv.py --input $< --output $@ --algorithm local-test

clean:
	rm -rf ./bin/ap_trampoline.bin
	rm -rf ./bin/os64.bin
	rm -rf ./bin/kernel64.bin
	rm -rf ./bin/kernel64.elf
	rm -rf ./bin/BOOTX64.EFI
	rm -rf ./bin/uefi_esp.img
	rm -rf ./bin/uefi_diag_esp.img
	rm -rf ./bin/OVMF_VARS_4M.fd
	rm -rf $(USER_BINS)
	rm -rf $(USER_ELFS)
	rm -rf $(USER_ELF_OBJECTS)
	rm -rf $(USER_C_OBJECTS)
	rm -rf $(DRIVER_PACKAGES)
	rm -rf $(DRIVER_ABI_FIXTURES)
	rm -rf $(USER_SDK_OBJECTS)
	rm -rf $(USER_SDK_LIB)
	rm -rf ./build/*
