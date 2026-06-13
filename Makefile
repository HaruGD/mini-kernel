# 1. 도구 설정
CC = i686-elf-gcc
CXX = i686-elf-g++
LD = i686-elf-ld
AS = nasm
HOST64_CC = gcc
HOST64_CXX = g++
HOST64_LD = ld
HOST64_OBJCOPY = objcopy
UEFI_CC = gcc
UEFI_LD = ld
UEFI_OBJCOPY = objcopy
STAGE2_SECTORS = 8
STAGE2_64_SECTORS = 12
STAGE2_MAX_SIZE = $(shell expr $(STAGE2_SECTORS) \* 512)
STAGE2_64_MAX_SIZE = $(shell expr $(STAGE2_64_SECTORS) \* 512)

# 2. 경로 및 플래그 설정
INCLUDES = -I./src/include -I./src
FLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0
CFLAGS = $(FLAGS) -std=gnu99 $(INCLUDES)
CPPFLAGS = $(FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit $(INCLUDES)
HOST64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
HOST64_CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -m64 -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
UEFI_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fshort-wchar -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer $(INCLUDES)
USER64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fpie -I./src/user/include
USER_ASM_SOURCES = $(wildcard ./src/user/*.asm)
USER_BINS = $(patsubst ./src/user/%.asm,./bin/%.bin,$(USER_ASM_SOURCES))
USER_EASM_SOURCES = $(filter-out ./src/user/user_crt0.easm,$(wildcard ./src/user/*.easm))
USER_ELF_OBJECTS = $(patsubst ./src/user/%.easm,./build/user_elf_%.o,$(USER_EASM_SOURCES))
USER_EASM_ELFS = $(patsubst ./src/user/%.easm,./bin/%.elf,$(USER_EASM_SOURCES))
# Reserve user_*.c for shared/runtime support so future helpers do not become
# standalone root filesystem apps by accident.
USER_C_RUNTIME_SOURCES = $(wildcard ./src/user/user_*.c)
USER_C_SOURCES = $(filter-out $(USER_C_RUNTIME_SOURCES),$(wildcard ./src/user/*.c))
USER_C_OBJECTS = $(patsubst ./src/user/%.c,./build/user_c_%.o,$(USER_C_SOURCES))
USER_C_ELFS = $(patsubst ./src/user/%.c,./bin/%.elf,$(USER_C_SOURCES))
USER_ELFS = $(USER_EASM_ELFS) $(USER_C_ELFS)
DRIVER_PACKAGES = ./bin/hello.drv
USER_EXTRA_ARGS = $(foreach file,$(USER_BINS) $(USER_ELFS) $(DRIVER_PACKAGES),--extra-file-auto $(file))

# 3. 오브젝트 파일 목록 (★순서가 가장 중요합니다★)
# kernel.asm.o가 무조건 맨 앞에 와야 EIP=0x2d 에러를 막을 수 있습니다.
FILES = ./build/kernel.asm.o \
        ./build/kernel.o \
        ./build/idt.o \
        ./build/pmm.o \
        ./build/heap.o \
        ./build/cpprt.o \
        ./build/terminal.o \
        ./build/keyboard.o \
        ./build/ata.o \
        ./build/fat12.o \
        ./build/paging.o \
        ./build/pit.o \
        ./build/shell.o

# 4. 빌드 규칙
all: ./bin/os64.bin
all32: ./bin/os.bin
all64: ./bin/os64.bin
uefi: ./bin/uefi_esp.img

# 최종 이미지 생성 (legacy FAT12 BIOS 부트 이미지 + KERNEL.BIN)
./bin/os.bin: ./bin/boot.bin ./bin/stage2.bin ./bin/kernel.bin ./tools/build_fat12_image.py
	python3 ./tools/build_fat12_image.py \
		--boot ./bin/boot.bin \
		--stage2 ./bin/stage2.bin \
		--kernel ./bin/kernel.bin \
		--output ./bin/os.bin \
		--stage2-sectors $(STAGE2_SECTORS)

./bin/os64.bin: ./bin/kernel64.bin $(USER_BINS) $(USER_ELFS) $(DRIVER_PACKAGES) ./tools/build_fat32_root_image.py
	python3 ./tools/build_fat32_root_image.py \
		--kernel ./bin/kernel64.bin \
		$(USER_EXTRA_ARGS) \
		--output ./bin/os64.bin

# 부트로더 컴파일
./bin/boot.bin: ./src/boot/boot.asm
	@mkdir -p ./bin
	$(AS) -DSTAGE2_SECTOR_COUNT=$(STAGE2_SECTORS) -f bin ./src/boot/boot.asm -o ./bin/boot.bin

./bin/boot64.bin: ./src/boot/boot.asm
	@mkdir -p ./bin
	$(AS) -DSTAGE2_SECTOR_COUNT=$(STAGE2_64_SECTORS) -f bin ./src/boot/boot.asm -o ./bin/boot64.bin

./bin/stage2.bin: ./src/boot/stage2.asm
	@mkdir -p ./bin
	$(AS) -DSTAGE2_SECTOR_COUNT=$(STAGE2_SECTORS) -f bin ./src/boot/stage2.asm -o ./bin/stage2.bin
	@stage2_size=$$(stat -c%s ./bin/stage2.bin); \
	if [ $$stage2_size -gt $(STAGE2_MAX_SIZE) ]; then \
		echo "stage2.bin is $$stage2_size bytes, but stage1 loads only $(STAGE2_MAX_SIZE) bytes"; \
		exit 1; \
	fi; \
	padding=$$(($(STAGE2_MAX_SIZE) - $$stage2_size)); \
	if [ $$padding -gt 0 ]; then \
		dd if=/dev/zero bs=1 count=$$padding >> ./bin/stage2.bin 2>/dev/null; \
	fi

./bin/stage2_64.bin: ./src/boot/stage2.asm
	@mkdir -p ./bin
	$(AS) -DSTAGE2_SECTOR_COUNT=$(STAGE2_64_SECTORS) -DLONG_MODE_KERNEL=1 -f bin ./src/boot/stage2.asm -o ./bin/stage2_64.bin
	@stage2_size=$$(stat -c%s ./bin/stage2_64.bin); \
	if [ $$stage2_size -gt $(STAGE2_64_MAX_SIZE) ]; then \
		echo "stage2_64.bin is $$stage2_size bytes, but stage1 loads only $(STAGE2_64_MAX_SIZE) bytes"; \
		exit 1; \
	fi; \
	padding=$$(($(STAGE2_64_MAX_SIZE) - $$stage2_size)); \
	if [ $$padding -gt 0 ]; then \
		dd if=/dev/zero bs=1 count=$$padding >> ./bin/stage2_64.bin 2>/dev/null; \
	fi

# 커널 링크 (링커 스크립트 사용)
./bin/kernel.bin: $(FILES)
	$(LD) -g -relocatable $(FILES) -o ./build/completeKernel.o
	$(LD) -T ./src/arch/x86/linkerScript.ld -o ./bin/kernel.bin ./build/completeKernel.o

./build/kernel64_entry.o: ./src/boot/kernel64_entry.asm
	@mkdir -p ./build
	$(AS) -f elf64 -g ./src/boot/kernel64_entry.asm -o ./build/kernel64_entry.o

./build/kernel64.o: ./src/kernel/kernel64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/kernel64.cpp -o ./build/kernel64.o

./build/kutil64.o: ./src/kernel/kutil64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/kutil64.cpp -o ./build/kutil64.o

./build/kernel_diag64.o: ./src/kernel/kernel_diag.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/kernel_diag.cpp -o ./build/kernel_diag64.o

./build/process64.o: ./src/kernel/process64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/process64.cpp -o ./build/process64.o

./build/userprog64.o: ./src/kernel/userprog64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/userprog64.cpp -o ./build/userprog64.o

./build/syscall64.o: ./src/kernel/syscall64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/syscall64.cpp -o ./build/syscall64.o

./build/ksh64.o: ./src/kernel/ksh64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/ksh64.cpp -o ./build/ksh64.o

./build/driver_manager64.o: ./src/kernel/driver/driver_manager.cpp ./src/include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/driver/driver_manager.cpp -o ./build/driver_manager64.o

./build/driver_exports64.o: ./src/kernel/driver/driver_exports.cpp ./src/include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/driver/driver_exports.cpp -o ./build/driver_exports64.o

./build/driver_loader64.o: ./src/kernel/driver/driver_loader.cpp ./src/include/kernel/driver/driver_manager.h ./src/include/kernel/driver/drv_format.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/driver/driver_loader.cpp -o ./build/driver_loader64.o

./build/driver_builtin64.o: ./src/kernel/driver/driver_builtin.cpp ./src/include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/driver/driver_builtin.cpp -o ./build/driver_builtin64.o

./build/driver_shell64.o: ./src/kernel/driver/driver_shell.cpp ./src/include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/driver/driver_shell.cpp -o ./build/driver_shell64.o

./build/kernel_exports64.o: ./src/kernel/driver/kernel_exports.cpp ./src/include/kernel/driver/kernel_exports.h ./src/include/kernel/driver/driver_manager.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/kernel/driver/kernel_exports.cpp -o ./build/kernel_exports64.o

./bin/hello.drv: ./tools/build_hello_drv.py
	@mkdir -p ./bin
	python3 ./tools/build_hello_drv.py --output ./bin/hello.drv

./build/terminal64.o: ./src/drivers/terminal.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/drivers/terminal.cpp -o ./build/terminal64.o

./build/ata64.o: ./src/drivers/ata.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/drivers/ata.cpp -o ./build/ata64.o

./build/fat32_64.o: ./src/fs/fat32/fat32.cpp ./src/fs/fat32/fat32_common.cpp ./src/fs/fat32/fat32_dir.cpp ./src/fs/fat32/fat32_lfn.cpp ./src/fs/fat32/fat32_cluster.cpp ./src/fs/fat32/fat32_api.cpp ./src/include/fs/fat32.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/fs/fat32/fat32.cpp -o ./build/fat32_64.o

./build/fat32_vfs64.o: ./src/fs/fat32/fat32_vfs.cpp ./src/include/fs/fat32.h ./src/include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/fs/fat32/fat32_vfs.cpp -o ./build/fat32_vfs64.o

./build/vfs64.o: ./src/fs/vfs.cpp ./src/fs/vfs/vfs_common.cpp ./src/fs/vfs/vfs_memfs.cpp ./src/fs/vfs/vfs_core.cpp ./src/fs/vfs/vfs_open.cpp ./src/include/fs/vfs.h
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/fs/vfs.cpp -o ./build/vfs64.o

./build/keyboard64.o: ./src/drivers/keyboard.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/drivers/keyboard.cpp -o ./build/keyboard64.o

./build/pit64.o: ./src/drivers/pit.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/drivers/pit.cpp -o ./build/pit64.o

./build/idt64.o: ./src/arch/x86/idt64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/arch/x86/idt64.cpp -o ./build/idt64.o

./build/idt64_asm.o: ./src/boot/idt64.asm
	$(AS) -f elf64 -g ./src/boot/idt64.asm -o ./build/idt64_asm.o

./build/gdt64.o: ./src/arch/x86/gdt64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/arch/x86/gdt64.cpp -o ./build/gdt64.o

./build/gdt64_asm.o: ./src/boot/gdt64.asm
	$(AS) -f elf64 -g ./src/boot/gdt64.asm -o ./build/gdt64_asm.o

./build/user64_asm.o: ./src/boot/user64.asm
	$(AS) -f elf64 -g ./src/boot/user64.asm -o ./build/user64_asm.o

./build/pmm64.o: ./src/arch/x86/pmm64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/arch/x86/pmm64.cpp -o ./build/pmm64.o

./build/paging64.o: ./src/arch/x86/paging64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/arch/x86/paging64.cpp -o ./build/paging64.o

./build/heap64.o: ./src/heap64.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -Os -c ./src/heap64.cpp -o ./build/heap64.o

./bin/kernel64.elf: ./build/kernel64_entry.o ./build/kernel64.o ./build/kutil64.o ./build/kernel_diag64.o ./build/process64.o ./build/userprog64.o ./build/syscall64.o ./build/ksh64.o ./build/driver_manager64.o ./build/driver_exports64.o ./build/driver_loader64.o ./build/driver_builtin64.o ./build/driver_shell64.o ./build/kernel_exports64.o ./build/terminal64.o ./build/ata64.o ./build/fat32_64.o ./build/fat32_vfs64.o ./build/vfs64.o ./build/keyboard64.o ./build/pit64.o ./build/idt64.o ./build/idt64_asm.o ./build/gdt64.o ./build/gdt64_asm.o ./build/user64_asm.o ./build/pmm64.o ./build/paging64.o ./build/heap64.o
	$(HOST64_LD) -m elf_x86_64 -nostdlib -T ./src/arch/x86/linkerScript64.ld -o ./bin/kernel64.elf ./build/kernel64_entry.o ./build/kernel64.o ./build/kutil64.o ./build/kernel_diag64.o ./build/process64.o ./build/userprog64.o ./build/syscall64.o ./build/ksh64.o ./build/driver_manager64.o ./build/driver_exports64.o ./build/driver_loader64.o ./build/driver_builtin64.o ./build/driver_shell64.o ./build/kernel_exports64.o ./build/terminal64.o ./build/ata64.o ./build/fat32_64.o ./build/fat32_vfs64.o ./build/vfs64.o ./build/keyboard64.o ./build/pit64.o ./build/idt64.o ./build/idt64_asm.o ./build/gdt64.o ./build/gdt64_asm.o ./build/user64_asm.o ./build/pmm64.o ./build/paging64.o ./build/heap64.o

./bin/kernel64.bin: ./bin/kernel64.elf
	$(HOST64_OBJCOPY) -O binary ./bin/kernel64.elf ./bin/kernel64.bin

./build/uefi_boot.o: ./src/uefi/uefi_boot.c ./src/include/kernel/boot_info.h
	@mkdir -p ./build
	$(UEFI_CC) $(UEFI_CFLAGS) -c ./src/uefi/uefi_boot.c -o ./build/uefi_boot.o

./build/uefi_entry.o: ./src/uefi/uefi_entry.asm
	@mkdir -p ./build
	$(AS) -f elf64 -g ./src/uefi/uefi_entry.asm -o ./build/uefi_entry.o

./build/BOOTX64.elf: ./build/uefi_entry.o ./build/uefi_boot.o ./src/uefi/uefi.ld
	$(UEFI_LD) -m elf_x86_64 -nostdlib -T ./src/uefi/uefi.ld -o ./build/BOOTX64.elf ./build/uefi_entry.o ./build/uefi_boot.o

./bin/BOOTX64.EFI: ./build/BOOTX64.elf
	@mkdir -p ./bin
	$(UEFI_OBJCOPY) -j .text -j .rodata -j .data -j .bss -O pei-x86-64 --subsystem efi-app --image-base 0x400000 --stack 0x100000,0x1000 ./build/BOOTX64.elf ./bin/BOOTX64.EFI

./bin/uefi_esp.img: ./bin/BOOTX64.EFI ./bin/kernel64.bin ./tools/build_uefi_esp.py
	python3 ./tools/build_uefi_esp.py --efi ./bin/BOOTX64.EFI --kernel ./bin/kernel64.bin --output ./bin/uefi_esp.img

./bin/fat32.img: ./tools/build_fat32_image.py
	@mkdir -p ./bin
	python3 ./tools/build_fat32_image.py --output ./bin/fat32.img

./bin/%.bin: ./src/user/%.asm
	@mkdir -p ./bin
	$(AS) -f bin -o $@ $<

./build/user_elf_%.o: ./src/user/%.easm
	@mkdir -p ./build
	$(AS) -f elf64 -g -o $@ $<

./build/user_c_%.o: ./src/user/%.c
	@mkdir -p ./build
	$(HOST64_CC) $(USER64_CFLAGS) -c $< -o $@

./build/user_crt0.o: ./src/user/user_crt0.easm
	@mkdir -p ./build
	$(AS) -f elf64 -g -o $@ $<

$(USER_EASM_ELFS): ./bin/%.elf: ./build/user_elf_%.o ./src/user/user_elf.ld
	@mkdir -p ./bin
	$(HOST64_LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T ./src/user/user_elf.ld -o $@ $<

$(USER_C_ELFS): ./bin/%.elf: ./build/user_c_%.o ./build/user_crt0.o ./src/user/user_elf.ld
	@mkdir -p ./bin
	$(HOST64_LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T ./src/user/user_elf.ld -o $@ ./build/user_crt0.o $<

./build/user_c_ushell_c.o: ./src/user/ushell/ushell_helpers.inc ./src/user/ushell/ushell_main.inc ./src/user/include/userlib.h ./src/user/include/userlib/userlib_syscalls.h ./src/user/include/userlib/userlib_text.h ./src/user/include/userlib/userlib_path_input.h

# --- 개별 소스 컴파일 (폴더 구조 반영) ---

./build/kernel.asm.o: ./src/boot/kernel.asm
	@mkdir -p ./build
	$(AS) -f elf -g ./src/boot/kernel.asm -o ./build/kernel.asm.o

./build/kernel.o: ./src/kernel/kernel.cpp
	$(CXX) $(CPPFLAGS) -c ./src/kernel/kernel.cpp -o ./build/kernel.o

./build/idt.o: ./src/arch/x86/idt.c
	$(CC) $(CFLAGS) -c ./src/arch/x86/idt.c -o ./build/idt.o

./build/pmm.o: ./src/arch/x86/pmm.c
	$(CC) $(CFLAGS) -c ./src/arch/x86/pmm.c -o ./build/pmm.o

./build/heap.o: ./src/heap.c
	$(CC) $(CFLAGS) -c ./src/heap.c -o ./build/heap.o

./build/cpprt.o: ./src/kernel/cpprt.cpp
	$(CXX) $(CPPFLAGS) -c ./src/kernel/cpprt.cpp -o ./build/cpprt.o

# 드라이버 폴더
./build/terminal.o: ./src/drivers/terminal.cpp
	$(CXX) $(CPPFLAGS) -c ./src/drivers/terminal.cpp -o ./build/terminal.o

./build/keyboard.o: ./src/drivers/keyboard.cpp
	$(CXX) $(CPPFLAGS) -c ./src/drivers/keyboard.cpp -o ./build/keyboard.o

./build/ata.o: ./src/drivers/ata.cpp
	$(CXX) $(CPPFLAGS) -c ./src/drivers/ata.cpp -o ./build/ata.o

./build/pit.o: ./src/drivers/pit.cpp
	$(CXX) $(CPPFLAGS) -c ./src/drivers/pit.cpp -o ./build/pit.o

# 기타 커널 소스
./build/fat12.o: ./src/fs/fat12.cpp
	$(CXX) $(CPPFLAGS) -c ./src/fs/fat12.cpp -o ./build/fat12.o

./build/paging.o: ./src/arch/x86/paging.cpp
	$(CXX) $(CPPFLAGS) -c ./src/arch/x86/paging.cpp -o ./build/paging.o

./build/shell.o: ./src/shell/shell.cpp
	$(CXX) $(CPPFLAGS) -c ./src/shell/shell.cpp -o ./build/shell.o

clean:
	rm -rf ./bin/os.bin
	rm -rf ./bin/os64.bin
	rm -rf ./bin/kernel.bin
	rm -rf ./bin/kernel64.bin
	rm -rf ./bin/kernel64.elf
	rm -rf ./bin/BOOTX64.EFI
	rm -rf ./bin/uefi_esp.img
	rm -rf ./bin/OVMF_VARS_4M.fd
	rm -rf ./bin/boot.bin
	rm -rf ./bin/boot64.bin
	rm -rf ./bin/stage2.bin
	rm -rf ./bin/stage2_64.bin
	rm -rf $(USER_BINS)
	rm -rf $(USER_ELFS)
	rm -rf $(USER_ELF_OBJECTS)
	rm -rf $(USER_C_OBJECTS)
	rm -rf ./build/user_crt0.o
	rm -rf ./build/*
