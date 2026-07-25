#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "arch/x86_64/io.h"
    #include "kernel/mm/heap.h"
}

#include "arch/x86_64/idt64.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/gdt64.h"
#include "kernel/mm/vm.h"
#include "kernel/mm/pmm.h"
#include "drivers/terminal.h"
#include "drivers/ata.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "fat32.h"
#include "fs/vfs.h"
#include "kernel/boot_info.h"
#include "kernel/acpi.h"
#include "kernel/cpu_local.h"
#include "kernel/smp.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/driver_manager.h"
#include "kernel/pci.h"
#include "kernel/kernel_diag.h"
#include "kernel/graphics/display_backend.h"
#include "kernel/graphics/display_owner.h"
#include "kernel/elf64.h"
#include "kernel/ksh64.h"
#include "kernel/kutil64.h"
#include "kernel/klog.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"
#include "kernel/syscall64.h"
#include "kernel/syscall/sdk_syscalls.h"
#include "kernel/userprog64.h"

#define USER_SLOT0_CODE_BASE  0x0000000009000000ULL
#define USER_SLOT0_STACK_BASE 0x0000000009100000ULL
#define USER_SLOT1_CODE_BASE  0x0000000009200000ULL
#define USER_SLOT1_STACK_BASE 0x0000000009300000ULL
#define USER_SLOT2_CODE_BASE  0x0000000009400000ULL
#define USER_SLOT2_STACK_BASE 0x0000000009500000ULL
#define USER_SLOT3_CODE_BASE  0x0000000009600000ULL
#define USER_SLOT3_STACK_BASE 0x0000000009700000ULL
#define USER_STACK_GUARD_PAGE_COUNT 1
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
extern "C" void enter_user_mode(uint64_t rip, uint64_t rsp);
extern "C" void resume_user_mode();

#define kernel_user_return_rsp (cpu_local_current()->user_state.return_rsp)
#define kernel_user_saved_rbx (cpu_local_current()->user_state.saved_rbx)
#define kernel_user_saved_rbp (cpu_local_current()->user_state.saved_rbp)
#define kernel_user_saved_r12 (cpu_local_current()->user_state.saved_r12)
#define kernel_user_saved_r13 (cpu_local_current()->user_state.saved_r13)
#define kernel_user_saved_r14 (cpu_local_current()->user_state.saved_r14)
#define kernel_user_saved_r15 (cpu_local_current()->user_state.saved_r15)
#define kernel_user_resume_rax (cpu_local_current()->user_state.resume_rax)
#define kernel_user_resume_rbx (cpu_local_current()->user_state.resume_rbx)
#define kernel_user_resume_rcx (cpu_local_current()->user_state.resume_rcx)
#define kernel_user_resume_rdx (cpu_local_current()->user_state.resume_rdx)
#define kernel_user_resume_rbp (cpu_local_current()->user_state.resume_rbp)
#define kernel_user_resume_rsi (cpu_local_current()->user_state.resume_rsi)
#define kernel_user_resume_rdi (cpu_local_current()->user_state.resume_rdi)
#define kernel_user_resume_r8 (cpu_local_current()->user_state.resume_r8)
#define kernel_user_resume_r9 (cpu_local_current()->user_state.resume_r9)
#define kernel_user_resume_r10 (cpu_local_current()->user_state.resume_r10)
#define kernel_user_resume_r11 (cpu_local_current()->user_state.resume_r11)
#define kernel_user_resume_r12 (cpu_local_current()->user_state.resume_r12)
#define kernel_user_resume_r13 (cpu_local_current()->user_state.resume_r13)
#define kernel_user_resume_r14 (cpu_local_current()->user_state.resume_r14)
#define kernel_user_resume_r15 (cpu_local_current()->user_state.resume_r15)
#define kernel_user_resume_rip (cpu_local_current()->user_state.resume_rip)
#define kernel_user_resume_rsp (cpu_local_current()->user_state.resume_rsp)
#define kernel_user_resume_rflags (cpu_local_current()->user_state.resume_rflags)

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
