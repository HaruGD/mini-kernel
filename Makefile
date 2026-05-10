# 1. 도구 설정
CC = i686-elf-gcc
CXX = i686-elf-g++
LD = i686-elf-ld
AS = nasm
HOST64_CC = gcc
HOST64_CXX = g++
HOST64_LD = ld
HOST64_OBJCOPY = objcopy
STAGE2_SECTORS = 8
STAGE2_64_SECTORS = 12
STAGE2_MAX_SIZE = $(shell expr $(STAGE2_SECTORS) \* 512)
STAGE2_64_MAX_SIZE = $(shell expr $(STAGE2_64_SECTORS) \* 512)

# 2. 경로 및 플래그 설정
INCLUDES = -I./src/include -I./src
FLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0
CFLAGS = $(FLAGS) -std=gnu99 $(INCLUDES)
CPPFLAGS = $(FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit $(INCLUDES)
HOST64_CFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -std=gnu11 -m64 -mno-red-zone -fno-pic -fno-pie $(INCLUDES)
HOST64_CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -m64 -mno-red-zone -fno-pic -fno-pie $(INCLUDES)

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

# 최종 이미지 생성 (FAT12 부트 이미지 + KERNEL.BIN)
./bin/os.bin: ./bin/boot.bin ./bin/stage2.bin ./bin/kernel.bin ./tools/build_fat12_image.py
	python3 ./tools/build_fat12_image.py \
		--boot ./bin/boot.bin \
		--stage2 ./bin/stage2.bin \
		--kernel ./bin/kernel.bin \
		--output ./bin/os.bin \
		--stage2-sectors $(STAGE2_SECTORS)

./bin/os64.bin: ./bin/boot64.bin ./bin/stage2_64.bin ./bin/kernel64.bin ./tools/build_fat12_image.py
	python3 ./tools/build_fat12_image.py \
		--boot ./bin/boot64.bin \
		--stage2 ./bin/stage2_64.bin \
		--kernel ./bin/kernel64.bin \
		--output ./bin/os64.bin \
		--stage2-sectors $(STAGE2_64_SECTORS)

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
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -c ./src/kernel/kernel64.cpp -o ./build/kernel64.o

./build/terminal64.o: ./src/drivers/terminal.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -c ./src/drivers/terminal.cpp -o ./build/terminal64.o

./build/ata64.o: ./src/drivers/ata.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -c ./src/drivers/ata.cpp -o ./build/ata64.o

./build/fat12_64.o: ./src/fs/fat12.cpp
	$(HOST64_CXX) $(HOST64_CPPFLAGS) -c ./src/fs/fat12.cpp -o ./build/fat12_64.o

./bin/kernel64.elf: ./build/kernel64_entry.o ./build/kernel64.o ./build/terminal64.o ./build/ata64.o ./build/fat12_64.o
	$(HOST64_LD) -m elf_x86_64 -nostdlib -T ./src/arch/x86/linkerScript64.ld -o ./bin/kernel64.elf ./build/kernel64_entry.o ./build/kernel64.o ./build/terminal64.o ./build/ata64.o ./build/fat12_64.o

./bin/kernel64.bin: ./bin/kernel64.elf
	$(HOST64_OBJCOPY) -O binary ./bin/kernel64.elf ./bin/kernel64.bin

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
	rm -rf ./bin/boot.bin
	rm -rf ./bin/boot64.bin
	rm -rf ./bin/stage2.bin
	rm -rf ./bin/stage2_64.bin
	rm -rf ./build/*
