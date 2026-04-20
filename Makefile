# INCLUDES = -I./src/include

# FILES = ./build/kernel.asm.o ./build/kernel.o ./build/idt.o ./build/pmm.o ./build/heap.o ./build/cpprt.o ./build/terminal.o ./build/keyboard.o ./build/ata.o ./build/fat12.o ./build/paging.o ./build/pit.o ./build/shell.o
# FLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc
# CPPFLAGS = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -fno-exceptions -fno-rtti -fno-use-cxa-atexit

# CFLAGS   += $(INCLUDES)
# CPPFLAGS += $(INCLUDES)

# all:
# 	nasm -f bin ./src/boot.asm -o ./bin/boot.bin
# 	nasm -f elf -g ./src/kernel.asm -o ./build/kernel.asm.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/kernel.cpp -o ./build/kernel.o
# 	i686-elf-gcc -I./src/include -g -ffreestanding -Wall -O0 -std=gnu99 -c ./src/idt.c -o ./build/idt.o
# 	i686-elf-gcc -I./src/include -g -ffreestanding -Wall -O0 -std=gnu99 -c ./src/pmm.c -o ./build/pmm.o
# 	i686-elf-gcc -I./src/include -g -ffreestanding -Wall -O0 -std=gnu99 -c ./src/heap.c -o ./build/heap.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/cpprt.cpp -o ./build/cpprt.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/drivers/terminal.cpp -o ./build/terminal.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/drivers/keyboard.cpp -o ./build/keyboard.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/drivers/ata.cpp -o ./build/ata.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/fat12.cpp -o ./build/fat12.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/paging.cpp -o ./build/paging.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/drivers/pit.cpp -o ./build/pit.o
# 	i686-elf-g++ -I./src/include $(CPPFLAGS) -c ./src/shell.cpp -o ./build/shell.o
# 	i686-elf-ld -g -relocatable $(FILES) -o ./build/completeKernel.o
# 	i686-elf-ld -T ./src/linkerScript.ld -o ./bin/kernel.bin ./build/completeKernel.o

# 	dd if=./bin/boot.bin >> ./bin/os.bin
# 	dd if=./bin/kernel.bin >> ./bin/os.bin
# 	dd if=/dev/zero bs=512 count=64 >> ./bin/os.bin

# clean:
# 	rm -f ./bin/boot.bin
# 	rm -f ./bin/kernel.bin
# 	rm -f ./bin/os.bin
# 	rm -f ./build/kernel.asm.o
# 	rm -f ./build/kernel.o
# 	rm -f ./build/completeKernel.o
# 	rm -f ./build/idt.o
# 	rm -f ./build/pmm.o
# 	rm -f ./build/heap.o
# 	rm -f ./build/cpprt.o
# 	rm -f ./build/terminal.o
# 	rm -f ./build/keyboard.o
# 	rm -f ./build/ata.o
# 	rm -f ./build/fat12.o
# 	rm -f ./build/paging.o
# 	rm -f ./build/pit.o
# 	rm -f ./build/shell.o
# # 1. 도구 및 경로 설정
# CC      = i686-elf-gcc
# CXX     = i686-elf-g++
# LD      = i686-elf-ld
# AS      = nasm
# BIN_DIR = ./bin
# SRC_DIR = ./src
# BUILD_DIR = ./build

# # 2. 컴파일 플래그
# INCLUDES = -I$(SRC_DIR)/include -I$(SRC_DIR)
# FLAGS    = -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0
# CFLAGS   = $(FLAGS) -std=gnu99 $(INCLUDES)
# CPPFLAGS = $(FLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit $(INCLUDES)

# # 3. 소스 파일 자동 탐색 (핵심!)
# # boot.asm은 바이너리로 따로 뽑아야 하므로 제외하고 검색합니다.
# SRCS_CPP := $(shell find $(SRC_DIR) -name '*.cpp')
# SRCS_C   := $(shell find $(SRC_DIR) -name '*.c')
# # kernel.asm만 컴파일 대상에 포함 (boot.asm 제외)
# SRCS_ASM := $(SRC_DIR)/kernel.asm

# # 4. 오브젝트 파일 경로 매핑
# # src/drivers/keyboard.cpp -> build/drivers/keyboard.o 형태로 변환하여 충돌 방지
# OBJS := $(SRCS_CPP:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
# OBJS += $(SRCS_C:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
# OBJS += $(BUILD_DIR)/kernel.asm.o

# # --- 빌드 규칙 ---

# all: $(BIN_DIR)/os.bin

# # 최종 OS 이미지 생성
# $(BIN_DIR)/os.bin: $(BIN_DIR)/boot.bin $(BIN_DIR)/kernel.bin
# 	cat $(BIN_DIR)/boot.bin $(BIN_DIR)/kernel.bin > $(BIN_DIR)/os.bin
# 	dd if=/dev/zero bs=512 count=64 >> $(BIN_DIR)/os.bin

# # 부트로더 빌드
# $(BIN_DIR)/boot.bin: $(SRC_DIR)/boot.asm
# 	@mkdir -p $(BIN_DIR)
# 	$(AS) -f bin $< -o $@

# # 커널 빌드 및 링크
# $(BIN_DIR)/kernel.bin: $(OBJS)
# 	$(LD) -g -relocatable $(OBJS) -o $(BUILD_DIR)/completeKernel.o
# 	$(LD) -T $(SRC_DIR)/linkerScript.ld -o $@ $(BUILD_DIR)/completeKernel.o

# # --- 개별 파일 컴파일 규칙 (패턴 매칭) ---

# # CPP 파일 컴파일
# $(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
# 	@mkdir -p $(dir $@)
# 	$(CXX) $(CPPFLAGS) -c $< -o $@

# # C 파일 컴파일
# $(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
# 	@mkdir -p $(dir $@)
# 	$(CC) $(CFLAGS) -c $< -o $@

# # ASM 파일 컴파일 (kernel.asm 등)
# $(BUILD_DIR)/kernel.asm.o: $(SRC_DIR)/kernel.asm
# 	@mkdir -p $(BUILD_DIR)
# 	$(AS) -f elf -g $< -o $@

# 	dd if=./bin/boot.bin >> ./bin/os.bin
# 	dd if=./bin/kernel.bin >> ./bin/os.bin
# 	dd if=/dev/zero bs=512 count=64 >> ./bin/os.bin

# clean:
# 	rm -rf boot.bin
# 	rm -rf kernel.bin
# 	rm -rf os.bin
# 	rm -rf $(BUILD_DIR)/*

# .PHONY: all clean

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
	$(LD) -T ./src/linkerScript.ld -o ./bin/kernel.bin ./build/completeKernel.o
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

./build/idt.o: ./src/idt.c
	$(CC) $(CFLAGS) -c ./src/idt.c -o ./build/idt.o

./build/pmm.o: ./src/pmm.c
	$(CC) $(CFLAGS) -c ./src/pmm.c -o ./build/pmm.o

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
./build/fat12.o: ./src/fat12.cpp
	$(CXX) $(CPPFLAGS) -c ./src/fat12.cpp -o ./build/fat12.o

./build/paging.o: ./src/paging.cpp
	$(CXX) $(CPPFLAGS) -c ./src/paging.cpp -o ./build/paging.o

./build/shell.o: ./src/shell.cpp
	$(CXX) $(CPPFLAGS) -c ./src/shell.cpp -o ./build/shell.o

clean:
	rm -rf ./bin/os.bin
	rm -rf ./bin/kernel.bin
	rm -rf ./bin/boot.bin
	rm -rf ./build/*
