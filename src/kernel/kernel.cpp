#include <stddef.h>
extern "C" {
    #include "arch/x86/paging.h"
}

#include "shell/shell.h"

inline void* operator new(size_t, void* ptr) { return ptr; }

extern "C" {
    #include "arch/x86/idt.h"
    #include "arch/x86/pmm.h"
    #include "heap.h"
    #include "arch/x86/io.h"
}
#include "drivers/terminal.h"
#include "kernel/kernel.h"
#include "drivers/keyboard.h"
#include "drivers/ata.h"
#include "fs/fat12.h"
#include "drivers/pit.h"
#include "kernel/boot_info.h"

PIT pit;

extern "C" void timer_handler_c() {
    pit.handle();
}

#define MAX_BUFFER_SIZE 256

const char kbd_US[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char kbd_US_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

Terminal terminal;
KeyboardDriver keyboard;
ATADriver ata;
ATADriver disk(0xF0);
FAT12Driver fat(&disk);
Paging paging;

extern "C" void debug_print(const char* str) {
    terminal.print(str);
}

extern "C" void debug_print_hex(uint32_t val) {
    terminal.print_hex(val);
}

void* notebook_ptr = 0;
char shell_buffer[MAX_BUFFER_SIZE];
int buffer_index = 0;

int strlen(const char* str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

extern "C" void keyboard_handler_c() {
    keyboard.handle();
}

static void print_boot_info(const BootInfo* boot_info) {
    if (!boot_info) {
        terminal.print("BootInfo: null\n");
        return;
    }

    if (boot_info->magic != BOOT_INFO_MAGIC) {
        terminal.print("BootInfo: bad magic ");
        terminal.print_hex(boot_info->magic);
        terminal.print("\n");
        return;
    }

    terminal.print("BootInfo ok\n");
    terminal.print("Boot drive: ");
    terminal.print_hex(boot_info->boot_drive);
    terminal.print("\nKernel load: ");
    terminal.print_hex(boot_info->kernel_load_addr);
    terminal.print("\nKernel sectors: ");
    terminal.print_hex(boot_info->kernel_sector_count);
    terminal.print("\n");
}

extern "C" void kernel_main(const BootInfo* boot_info) {
    terminal.clear();
    print_boot_info(boot_info);

    set_idt();
    pmm_init();
    heap_init();

    paging.init();
    paging.enable();

    keyboard.init();
    ata.init();
    disk.init();
    pit.init();

    __asm__ volatile("sti");

    terminal.print("sti done\n");

    //dump_heap();

    fat.init();

    terminal.print("fat done\n");

    terminal.print("Welcome to MyOS Shell!\n");
    terminal.print("OS-Kernel> ");

    while(1) {}
}
