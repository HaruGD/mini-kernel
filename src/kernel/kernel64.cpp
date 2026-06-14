#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "arch/x86/io.h"
    #include "heap.h"
}

#include "arch/x86/idt64.h"
#include "arch/x86/gdt64.h"
#include "arch/x86/paging64.h"
#include "arch/x86/pmm64.h"
#include "drivers/terminal.h"
#include "drivers/ata.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "fs/fat32.h"
#include "fs/vfs.h"
#include "kernel/boot_info.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/driver_manager.h"
#include "kernel/kernel_diag.h"
#include "kernel/elf64.h"
#include "kernel/ksh64.h"
#include "kernel/kutil64.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"

#define USER_SLOT0_CODE_BASE  0x0000000009000000ULL
#define USER_SLOT0_STACK_BASE 0x0000000009100000ULL
#define USER_SLOT1_CODE_BASE  0x0000000009200000ULL
#define USER_SLOT1_STACK_BASE 0x0000000009300000ULL
#define USER_SLOT2_CODE_BASE  0x0000000009400000ULL
#define USER_SLOT2_STACK_BASE 0x0000000009500000ULL
#define USER_SLOT3_CODE_BASE  0x0000000009600000ULL
#define USER_SLOT3_STACK_BASE 0x0000000009700000ULL
#define USER_STACK_PAGE_COUNT 4
#define USER_PATH_MAX PROCESS_CMDLINE_MAX

Terminal terminal;
ATADriver ata;
KeyboardDriver keyboard;
PIT pit;
FAT32Driver fat32(&ata);
FAT32Driver ramdisk_fat32((uint8_t*)0, 0);
FAT32Driver* root_fat32 = &fat32;

static const BootInfo* g_boot_info = 0;
static uint64_t boot_tsc = 0;
static uint32_t user_test_count = 0;
static volatile int user_input_mode = 0;

extern "C" void enter_user_mode(uint64_t rip, uint64_t rsp);
extern "C" void resume_user_mode();
extern "C" uint64_t kernel_user_return_rsp;
extern "C" uint64_t kernel_user_saved_rbx;
extern "C" uint64_t kernel_user_saved_rbp;
extern "C" uint64_t kernel_user_saved_r12;
extern "C" uint64_t kernel_user_saved_r13;
extern "C" uint64_t kernel_user_saved_r14;
extern "C" uint64_t kernel_user_saved_r15;
extern "C" uint64_t kernel_user_resume_rax;
extern "C" uint64_t kernel_user_resume_rbx;
extern "C" uint64_t kernel_user_resume_rcx;
extern "C" uint64_t kernel_user_resume_rdx;
extern "C" uint64_t kernel_user_resume_rbp;
extern "C" uint64_t kernel_user_resume_rsi;
extern "C" uint64_t kernel_user_resume_rdi;
extern "C" uint64_t kernel_user_resume_r8;
extern "C" uint64_t kernel_user_resume_r9;
extern "C" uint64_t kernel_user_resume_r10;
extern "C" uint64_t kernel_user_resume_r11;
extern "C" uint64_t kernel_user_resume_r12;
extern "C" uint64_t kernel_user_resume_r13;
extern "C" uint64_t kernel_user_resume_r14;
extern "C" uint64_t kernel_user_resume_r15;
extern "C" uint64_t kernel_user_resume_rip;
extern "C" uint64_t kernel_user_resume_rsp;
extern "C" uint64_t kernel_user_resume_rflags;

static uint64_t current_rsp() {
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static void early_framebuffer_marker(const BootInfo* boot_info, uint32_t slot, uint32_t color) {
    if (boot_info == 0 ||
        boot_info->size < sizeof(BootInfo) ||
        !(boot_info->flags & BOOT_INFO_FLAG_FRAMEBUFFER) ||
        boot_info->framebuffer_addr == 0 ||
        boot_info->framebuffer_width == 0 ||
        boot_info->framebuffer_height == 0 ||
        boot_info->framebuffer_pixels_per_scanline == 0) {
        return;
    }

    volatile uint32_t* fb = (volatile uint32_t*)(uintptr_t)boot_info->framebuffer_addr;
    uint32_t box = 18;
    uint32_t gap = 4;
    uint32_t step = box + gap;
    if (boot_info->framebuffer_width < (slot + 1) * step ||
        boot_info->framebuffer_height < step) {
        return;
    }

    uint32_t x0 = boot_info->framebuffer_width - ((slot + 1) * step);
    uint32_t y0 = boot_info->framebuffer_height - step;
    for (uint32_t y = 0; y < box; y++) {
        for (uint32_t x = 0; x < box; x++) {
            fb[(uint64_t)(y0 + y) * boot_info->framebuffer_pixels_per_scanline + (x0 + x)] = color;
        }
    }
}

int resume_user_program(uint32_t pid);

#include "kernel/core/kernel64_process.cpp"
#include "kernel/core/kernel64_diag.cpp"
#include "kernel/core/kernel64_user.cpp"
#include "kernel/core/kernel64_irq.cpp"
#include "kernel/core/kernel64_main.cpp"
