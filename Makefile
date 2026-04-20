# 1. 도구 설정
CC = i686-elf-gcc
CXX = i686-elf-g++
LD = i686-elf-ld
AS = nasm
KERNEL_SECTORS = 32
KERNEL_MAX_SIZE = $(shell expr $(KERNEL_SECTORS) \* 512)

# 2. 경로 및 플래그 설정
INCLUDES = -I./src/include -I./src
FLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0
CFLAGS = $(FLAGS) -std=gnu99 $(INCLUDES)
CPPFLAGS = $(FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit $(INCLUDES)

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
all: ./bin/os.bin

# 최종 이미지 생성 (부트로더 + 커널 + 패딩)
./bin/os.bin: ./bin/boot.bin ./bin/kernel.bin
	cat ./bin/boot.bin ./bin/kernel.bin > ./bin/os.bin
	dd if=/dev/zero bs=512 count=64 >> ./bin/os.bin

# 부트로더 컴파일
./bin/boot.bin: ./src/boot.asm
	@mkdir -p ./bin
	$(AS) -DKERNEL_SECTOR_COUNT=$(KERNEL_SECTORS) -f bin ./src/boot.asm -o ./bin/boot.bin

# 커널 링크 (링커 스크립트 사용)
./bin/kernel.bin: $(FILES)
	$(LD) -g -relocatable $(FILES) -o ./build/completeKernel.o
	$(LD) -T ./src/arch/x86/linkerScript.ld -o ./bin/kernel.bin ./build/completeKernel.o
	@kernel_size=$$(stat -c%s ./bin/kernel.bin); \
	if [ $$kernel_size -gt $(KERNEL_MAX_SIZE) ]; then \
		echo "kernel.bin is $$kernel_size bytes, but bootloader loads only $(KERNEL_MAX_SIZE) bytes"; \
		exit 1; \
	fi

# --- 개별 소스 컴파일 (폴더 구조 반영) ---

./build/kernel.asm.o: ./src/kernel.asm
	@mkdir -p ./build
	$(AS) -f elf -g ./src/kernel.asm -o ./build/kernel.asm.o

./build/kernel.o: ./src/kernel.cpp
	$(CXX) $(CPPFLAGS) -c ./src/kernel.cpp -o ./build/kernel.o

./build/idt.o: ./src/arch/x86/idt.c
	$(CC) $(CFLAGS) -c ./src/arch/x86/idt.c -o ./build/idt.o

./build/pmm.o: ./src/arch/x86/pmm.c
	$(CC) $(CFLAGS) -c ./src/arch/x86/pmm.c -o ./build/pmm.o

./build/heap.o: ./src/heap.c
	$(CC) $(CFLAGS) -c ./src/heap.c -o ./build/heap.o

./build/cpprt.o: ./src/cpprt.cpp
	$(CXX) $(CPPFLAGS) -c ./src/cpprt.cpp -o ./build/cpprt.o

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
	rm -rf ./bin/kernel.bin
	rm -rf ./bin/boot.bin
	rm -rf ./build/*
