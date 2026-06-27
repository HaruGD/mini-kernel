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
INCLUDES = -I./include -I.
HOST64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
HOST64_CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -m64 -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables $(INCLUDES)
UEFI_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fshort-wchar -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
USER64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fpie -fno-stack-protector -I./user/include -I./user/sdk/include
DRIVER64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mcmodel=large -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer -I./drivers/external/include
DRIVER64_CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu++17 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -m64 -mcmodel=large -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer -I./drivers/external/include

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
USER_SDK_HEADERS = $(wildcard ./user/sdk/include/os64/*.h) $(wildcard ./user/sdk/src/*.h) ./include/os64/input_types.h


# External drivers
DRIVER_PROJECT_MANIFESTS = $(wildcard ./drivers/external/*/driver.json)
DRIVER_PROJECTS = $(patsubst ./drivers/external/%/driver.json,%,$(DRIVER_PROJECT_MANIFESTS))
DRIVER_PROJECT_OBJECTS = $(patsubst %,./build/driver_ext_%.o,$(DRIVER_PROJECTS))
DRIVER_PROJECT_PACKAGES = $(patsubst %,./bin/%.drv,$(DRIVER_PROJECTS))
DRIVER_PACKAGES = ./bin/hello.drv ./bin/provider.drv ./bin/consumer.drv $(DRIVER_PROJECT_PACKAGES)
USER_EXTRA_ARGS = $(foreach file,$(USER_BINS) $(USER_ELFS) $(DRIVER_PACKAGES),--extra-file-auto $(file))

.SECONDARY: $(DRIVER_PROJECT_OBJECTS)
.SECONDARY: $(patsubst %,./build/driver_ext_%.unsigned.drv,$(DRIVER_PROJECTS))
.PHONY: all all64 uefi uefi-diagnostic drivers test-user-sdk test-phase1 test-shutdown test-graphics-contracts test-graphics-demo test-gop-present test-graphics-clip test-input-queue clean

KERNEL64_OBJECTS = \
	./build/kernel64_entry.o \
	./build/kernel64.o \
	./build/kutil64.o \
	./build/kernel_diag64.o \
	./build/process64.o \
	./build/userprog64.o \
	./build/syscall64.o \
	./build/sdk_syscalls64.o \
	./build/klog64.o \
	./build/panic64.o \
	./build/acpi64.o \
	./build/acpi_power64.o \
	./build/apic64.o \
	./build/ksh64.o \
	./build/driver_manager64.o \
	./build/driver_exports64.o \
	./build/driver_binding64.o \
	./build/driver_irq64.o \
	./build/driver_loader64.o \
	./build/driver_unload64.o \
	./build/driver_builtin64.o \
	./build/driver_shell64.o \
	./build/kernel_exports64.o \
	./build/pci64.o \
	./build/input_event_queue64.o \
	./build/input_events64.o \
	./build/graphics_clip64.o \
	./build/graphics_surface64.o \
	./build/graphics_draw64.o \
	./build/graphics_dirty64.o \
	./build/graphics_present64.o \
	./build/graphics_font64.o \
	./build/display_owner64.o \
	./build/terminal64.o \
	./build/gop64.o \
	./build/ata64.o \
	./build/fat32_64.o \
	./build/fat32_vfs64.o \
	./build/vfs64.o \
	./build/keyboard64.o \
	./build/pit64.o \
	./build/idt64.o \
	./build/idt64_asm.o \
	./build/gdt64.o \
	./build/gdt64_asm.o \
	./build/user64_asm.o \
	./build/pmm64.o \
	./build/paging64.o \
	./build/heap64.o

all: all64
all64: ./bin/os64.bin
uefi: ./bin/uefi_esp.img ./bin/OVMF_VARS_4M.fd
uefi-diagnostic: ./bin/uefi_diag_esp.img ./bin/OVMF_VARS_4M.fd
drivers: $(DRIVER_PACKAGES)
test-user-sdk: uefi
	bash ./tools/run_usdk_test.sh
test-phase1: uefi uefi-diagnostic
	python3 ./tools/phase1_smoke.py
test-shutdown: uefi
	python3 ./tools/acpi_shutdown_smoke.py
test-graphics-contracts:
	python3 ./tools/graphics_clip_test.py
	python3 ./tools/graphics_surface_test.py
	python3 ./tools/graphics_draw_test.py
	python3 ./tools/graphics_dirty_test.py
	python3 ./tools/graphics_dirty_present_test.py
	python3 ./tools/graphics_font_test.py
test-graphics-clip: test-graphics-contracts
test-graphics-demo: uefi
	python3 ./tools/graphics_demo_smoke.py
test-gop-present: uefi
	python3 ./tools/gop_present_smoke.py
test-input-queue:
	python3 ./tools/input_event_queue_test.py
	python3 ./tools/keyboard_event_translation_test.py

all32:
	@echo "legacy BIOS build is archived under archive/legacy-bios and is not part of the active build."
	@exit 1

./bin/os64.bin: ./bin/kernel64.bin $(USER_BINS) $(USER_ELFS) $(DRIVER_PACKAGES) ./tools/build_fat32_root_image.py
	python3 ./tools/build_fat32_root_image.py \
		--kernel ./bin/kernel64.bin \
		$(USER_EXTRA_ARGS) \
		--output ./bin/os64.bin

./build/kernel64_entry.o: ./arch/x86_64/kernel64_entry.asm
	@mkdir -p ./build
	$(AS) -f elf64 -g $< -o $@

./build/kernel64.o: ./kernel/core/kernel64.cpp ./kernel/core/kernel64_main.cpp ./kernel/core/kernel64_process.cpp ./kernel/core/kernel64_diag.cpp ./kernel/core/kernel64_user.cpp ./kernel/core/kernel64_irq.cpp ./include/drivers/gop.h ./include/drivers/keyboard.h ./include/drivers/pit.h ./include/kernel/process.h ./include/kernel/syscall64.h
	@mkdir -p ./build
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/core/kernel64.cpp -o $@

./build/kutil64.o: ./kernel/util/kutil64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kernel_diag64.o: ./kernel/process/kernel_diag.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/process64.o: ./kernel/process/process64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/userprog64.o: ./kernel/process/userprog64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/syscall64.o: ./kernel/syscall/syscall64.cpp ./kernel/syscall/sdk_syscalls.h ./include/drivers/keyboard.h ./include/fs/vfs.h ./include/kernel/syscall64.h ./include/kernel/userprog64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/sdk_syscalls64.o: ./kernel/syscall/sdk_syscalls.cpp ./kernel/syscall/sdk_syscalls.h ./include/drivers/gop.h ./include/drivers/keyboard.h ./include/drivers/pit.h ./include/kernel/input/input_events.h ./include/kernel/syscall64.h ./include/kernel/userprog64.h ./include/os64/graphics_types.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/klog64.o: ./kernel/log/klog.cpp ./include/kernel/klog.h ./include/kernel/kutil64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/panic64.o: ./kernel/panic/panic.cpp ./include/kernel/panic.h ./include/kernel/klog.h ./include/kernel/boot_info.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/acpi64.o: ./kernel/acpi/acpi.cpp ./include/kernel/acpi.h ./include/kernel/klog.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/acpi_power64.o: ./kernel/acpi/acpi_power.cpp ./include/kernel/acpi.h ./include/arch/x86_64/io.h ./include/arch/x86_64/paging64.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/apic64.o: ./arch/x86_64/apic.cpp ./include/arch/x86_64/apic.h ./include/kernel/acpi.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/ksh64.o: ./kernel/shell/ksh64.cpp ./include/kernel/pci.h ./include/drivers/gop.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/shell/ksh64.cpp -o $@

./build/driver_manager64.o: ./kernel/driver/driver_manager.cpp ./include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_exports64.o: ./kernel/driver/driver_exports.cpp ./include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_binding64.o: ./kernel/driver/driver_binding.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/pci.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_irq64.o: ./kernel/driver/driver_irq.cpp ./include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_loader64.o: ./kernel/driver/driver_loader.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/drv_format.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_unload64.o: ./kernel/driver/driver_unload.cpp ./include/kernel/driver/driver_manager.h ./include/kernel/driver/drv_format.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_builtin64.o: ./kernel/driver/driver_builtin.cpp ./include/kernel/driver/driver_manager.h ./include/drivers/gop.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/driver_shell64.o: ./kernel/driver/driver_shell.cpp ./include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/kernel_exports64.o: ./kernel/driver/kernel_exports.cpp ./include/kernel/driver/kernel_exports.h ./include/kernel/driver/driver_manager.h ./include/kernel/pci.h ./include/arch/x86_64/io.h ./include/drivers/ata.h ./include/drivers/gop.h ./include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./kernel/driver/kernel_exports.cpp -o $@

./build/pci64.o: ./kernel/pci/pci.cpp ./include/kernel/pci.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/input_event_queue64.o: ./kernel/input/input_event_queue.cpp ./include/kernel/input/input_event_queue.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/input_events64.o: ./kernel/input/input_events.cpp ./include/kernel/input/input_events.h ./include/kernel/input/input_event_queue.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_clip64.o: ./kernel/graphics/graphics_clip.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_surface64.o: ./kernel/graphics/graphics_surface.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_draw64.o: ./kernel/graphics/graphics_draw.cpp ./include/kernel/graphics/graphics2d.h ./include/kernel/graphics/graphics_font.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_dirty64.o: ./kernel/graphics/graphics_dirty.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_present64.o: ./kernel/graphics/graphics_present.cpp ./include/kernel/graphics/graphics2d.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/graphics_font64.o: ./kernel/graphics/graphics_font.cpp ./include/kernel/graphics/graphics_font.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/display_owner64.o: ./kernel/graphics/display_owner.cpp ./include/kernel/graphics/display_owner.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/terminal64.o: ./drivers/builtin/terminal/terminal.cpp ./include/kernel/graphics/graphics_font.h ./include/kernel/graphics/display_owner.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/gop64.o: ./drivers/builtin/gop/gop.cpp ./include/drivers/gop.h ./include/kernel/boot_info.h ./include/kernel/graphics/graphics2d.h ./include/kernel/graphics/display_owner.h ./include/os64/graphics_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/ata64.o: ./drivers/builtin/ata/ata.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/keyboard64.o: ./drivers/builtin/keyboard/keyboard.cpp ./include/drivers/keyboard.h ./include/kernel/input/input_events.h ./include/os64/input_types.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/pit64.o: ./drivers/builtin/pit/pit.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/fat32_64.o: ./fs/fat32/fat32.cpp ./fs/fat32/fat32_common.cpp ./fs/fat32/fat32_dir.cpp ./fs/fat32/fat32_lfn.cpp ./fs/fat32/fat32_cluster.cpp ./fs/fat32/fat32_api.cpp ./include/fs/fat32.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./fs/fat32/fat32.cpp -o $@

./build/fat32_vfs64.o: ./fs/fat32/fat32_vfs.cpp ./include/fs/fat32.h ./include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/vfs64.o: ./fs/vfs/vfs.cpp ./fs/vfs/vfs_common.cpp ./fs/vfs/vfs_memfs.cpp ./fs/vfs/vfs_core.cpp ./fs/vfs/vfs_open.cpp ./include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./fs/vfs/vfs.cpp -o $@

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

./build/pmm64.o: ./arch/x86_64/pmm64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/paging64.o: ./arch/x86_64/paging64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c $< -o $@

./build/heap64.o: ./kernel/memory/heap64.cpp
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

./bin/uefi_esp.img: ./bin/BOOTX64.EFI ./bin/kernel64.bin ./bin/os64.bin ./tools/build_uefi_esp.py
	python3 ./tools/build_uefi_esp.py --efi ./bin/BOOTX64.EFI --kernel ./bin/kernel64.bin --root ./bin/os64.bin --output ./bin/uefi_esp.img

./bin/uefi_diag_esp.img: ./bin/BOOTX64.EFI ./bin/kernel64.bin ./bin/os64.bin ./tools/build_uefi_esp.py
	python3 ./tools/build_uefi_esp.py --efi ./bin/BOOTX64.EFI --kernel ./bin/kernel64.bin --root ./bin/os64.bin --diagnostic --output ./bin/uefi_diag_esp.img

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
	$(HOST64_LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T ./user/programs/user_elf.ld -o $@ ./build/user_crt0.o $< $(USER_SDK_LIB)

./build/user_c_ushell_c.o: ./user/programs/ushell/ushell_helpers.inc ./user/programs/ushell/ushell_main.inc ./user/include/userlib.h ./user/include/userlib/userlib_syscalls.h ./user/include/userlib/userlib_text.h ./user/include/userlib/userlib_path_input.h

./build/driver_ext_%.o: ./drivers/external/%/driver.c ./drivers/external/include/os64_driver.h
	@mkdir -p ./build
	$(HOST64_CC) $(DRIVER64_CFLAGS) -c $< -o $@

./build/driver_ext_%.o: ./drivers/external/%/driver.cpp ./drivers/external/include/os64_driver.h
	@mkdir -p ./build
	$(HOST64_CXX) $(DRIVER64_CPPFLAGS) -c $< -o $@

./build/driver_ext_%.unsigned.drv: ./build/driver_ext_%.o ./drivers/external/%/driver.json ./tools/driver_builder/build_drv.py
	@mkdir -p ./build
	python3 ./tools/driver_builder/build_drv.py --object $< --output $@ --manifest ./drivers/external/$*/driver.json

./bin/%.drv: ./build/driver_ext_%.unsigned.drv ./tools/driver_builder/sign_drv.py
	@mkdir -p ./bin
	python3 ./tools/driver_builder/sign_drv.py --input $< --output $@ --algorithm local-test

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
	rm -rf $(DRIVER_PROJECT_OBJECTS)
	rm -rf $(DRIVER_PROJECT_PACKAGES)
	rm -rf $(USER_SDK_OBJECTS)
	rm -rf $(USER_SDK_LIB)
	rm -rf ./build/*
