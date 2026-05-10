#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "arch/x86/io.h"
    #include "heap.h"
}

#include "arch/x86/idt64.h"
#include "arch/x86/pmm64.h"
#include "drivers/terminal.h"
#include "drivers/ata.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "fs/fat12.h"
#include "kernel/boot_info.h"

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY 10
#define MAX_CMD_LEN 80
#define NOTEBOOK_CAPACITY 4096
#define PROMPT "OS64> "

Terminal terminal;
ATADriver ata;
KeyboardDriver keyboard;
PIT pit;
FAT12Driver fat(&ata);

static const BootInfo* g_boot_info = 0;
static char shell_buffer[MAX_BUFFER_SIZE];
static int buffer_index = 0;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_index = history_count;
static char* notebook_ptr = 0;
static uint32_t notebook_length = 0;
static uint64_t boot_tsc = 0;

static int strlen64(const char* str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

static int strcmp64(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return ((unsigned char)*a) - ((unsigned char)*b);
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static void serial_init() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static int serial_ready() {
    return inb(0x3F8 + 5) & 0x20;
}

static void serial_putchar(char c) {
    while (!serial_ready()) {
    }
    outb(0x3F8, (unsigned char)c);
}

static void putchar_both(char c) {
    terminal.putchar(c);
    if (c == '\n') {
        serial_putchar('\r');
    }
    serial_putchar(c);
}

static void print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        putchar_both(str[i]);
    }
}

static void print_hex32(uint32_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buffer[i] = hex_chars[value & 0x0F];
        value >>= 4;
    }
    buffer[10] = '\0';
    print(buffer);
}

static void print_hex64(uint64_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 17; i >= 2; i--) {
        buffer[i] = hex_chars[(uint32_t)(value & 0x0F)];
        value >>= 4;
    }
    buffer[18] = '\0';
    print(buffer);
}

extern "C" void debug_print64(const char* str) {
    print(str);
}

extern "C" void debug_print_hex64(uint32_t value) {
    print_hex32(value);
}

extern "C" void debug_print_hex64_u64(uint64_t value) {
    print_hex64(value);
}

static uint64_t read_tsc() {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void to_name83(const char* input, char output[11]) {
    for (int i = 0; i < 11; i++) {
        output[i] = ' ';
    }

    int i = 0;
    int j = 0;
    while (input[i] != '.' && input[i] != '\0' && j < 8) {
        output[j++] = to_upper_ascii(input[i]);
        i++;
    }

    if (input[i] == '.') {
        i++;
    }

    j = 8;
    while (input[i] != '\0' && j < 11) {
        output[j++] = to_upper_ascii(input[i]);
        i++;
    }
}

static char* get_argument(char* input) {
    while (*input != ' ' && *input != '\0') {
        input++;
    }
    if (*input == ' ') {
        return input + 1;
    }
    return 0;
}

static uint64_t e820_base(const E820Entry* entry) {
    return ((uint64_t)entry->base_high << 32) | entry->base_low;
}

static uint64_t e820_length(const E820Entry* entry) {
    return ((uint64_t)entry->length_high << 32) | entry->length_low;
}

static void print_e820_entry(const E820Entry* entry, uint32_t index) {
    print("\n[");
    print_hex32(index);
    print("] base=");
    print_hex64(e820_base(entry));
    print(" len=");
    print_hex64(e820_length(entry));
    print(" type=");
    print_hex32(entry->type);
}

static void print_boot_info() {
    if (g_boot_info == 0) {
        print("\nBootInfo: null");
        return;
    }

    print("\nBootInfo magic: ");
    print_hex32(g_boot_info->magic);
    print("\nVersion: ");
    print_hex32(g_boot_info->version);
    print("\nBoot drive: ");
    print_hex32(g_boot_info->boot_drive);
    print("\nKernel load: ");
    print_hex32(g_boot_info->kernel_load_addr);
    print("\nKernel sectors: ");
    print_hex32(g_boot_info->kernel_sector_count);
    print("\nKernel bytes: ");
    print_hex32(g_boot_info->kernel_file_size);
    print("\nStage2 load: ");
    print_hex32(g_boot_info->stage2_load_addr);
    print("\nMemory map addr: ");
    print_hex32(g_boot_info->memory_map_addr);
    print("\nMemory map entries: ");
    print_hex32(g_boot_info->memory_map_entry_count);
    print("\nMemory map entry size: ");
    print_hex32(g_boot_info->memory_map_entry_size);
}

static void print_memmap() {
    if (g_boot_info == 0) {
        print("\nNo BootInfo.");
        return;
    }

    const E820Entry* entries = (const E820Entry*)(uintptr_t)g_boot_info->memory_map_addr;
    for (uint32_t i = 0; i < g_boot_info->memory_map_entry_count; i++) {
        print_e820_entry(&entries[i], i);
    }
}

static void dump_state() {
    print("\n=== OS64 STATE ===");
    print("\nBootInfo ptr: ");
    print_hex64((uint64_t)(uintptr_t)g_boot_info);
    print("\nInput len: ");
    print_hex32((uint32_t)buffer_index);
    print("\nHistory count: ");
    print_hex32((uint32_t)history_count);
    print("\nNotebook bytes: ");
    print_hex32(notebook_length);
    print("\nPMM free pages: ");
    print_hex32(pmm64_get_free_block_count());
    print("\nHeap used bytes: ");
    print_hex64(heap_total_used());
    print("\nHeap free bytes: ");
    print_hex64(heap_total_free());
    print("\nPIT tick: ");
    print_hex32(pit.get_tick());
    print("\n==================");
}

static void save_history() {
    if (buffer_index == 0) {
        return;
    }

    int save_index = history_count % MAX_HISTORY;
    int copy_len = buffer_index;
    if (copy_len >= MAX_CMD_LEN) {
        copy_len = MAX_CMD_LEN - 1;
    }

    for (int i = 0; i < copy_len; i++) {
        history[save_index][i] = shell_buffer[i];
    }
    history[save_index][copy_len] = '\0';

    history_count++;
    history_index = history_count;
}

extern "C" void shell_recall_history(int direction) {
    if (history_count == 0) {
        return;
    }

    int available = history_count < MAX_HISTORY ? history_count : MAX_HISTORY;
    int oldest_index = history_count - available;
    int new_index = history_index + direction;

    if (new_index < oldest_index) {
        new_index = oldest_index;
    }
    if (new_index > history_count) {
        new_index = history_count;
    }
    if (new_index == history_index) {
        return;
    }

    while (buffer_index > 0) {
        buffer_index--;
        terminal.putchar('\b');
        serial_putchar('\b');
        serial_putchar(' ');
        serial_putchar('\b');
    }

    history_index = new_index;
    if (history_index == history_count) {
        shell_buffer[0] = '\0';
        return;
    }

    int actual_index = history_index % MAX_HISTORY;
    char* recalled = history[actual_index];
    buffer_index = 0;
    for (int i = 0; recalled[i] != '\0' && buffer_index < MAX_BUFFER_SIZE - 1; i++) {
        shell_buffer[buffer_index++] = recalled[i];
        terminal.putchar(recalled[i]);
        serial_putchar(recalled[i]);
    }
    shell_buffer[buffer_index] = '\0';
}

static void command_help() {
    print("\nAvailable commands: help, clear, version, bootinfo, memmap, memstat, echo, write, read");
    print("\nfree, dump, atatest, ls, load, save, rm, pagefault, uptime");
}

static void command_memstat() {
    print("\nPMM total pages: ");
    print_hex32(pmm64_get_total_block_count());
    print("\nPMM free pages: ");
    print_hex32(pmm64_get_free_block_count());
    print("\nHeap used bytes: ");
    print_hex64(heap_total_used());
    print("\nHeap free bytes: ");
    print_hex64(heap_total_free());
}

static void command_echo(char* arg) {
    if (arg == 0) {
        print("\nUsage: echo [text]");
        return;
    }
    print("\n");
    print(arg);
}

static void command_write(char* arg) {
    if (arg == 0) {
        print("\nUsage: write [message]");
        return;
    }

    int len = strlen64(arg);
    if (len >= NOTEBOOK_CAPACITY) {
        len = NOTEBOOK_CAPACITY - 1;
    }

    if (notebook_ptr != 0) {
        kfree(notebook_ptr);
        notebook_ptr = 0;
    }

    notebook_ptr = (char*)kmalloc((size_t)len + 1);
    if (notebook_ptr == 0) {
        print("\nOut of memory.");
        notebook_length = 0;
        return;
    }

    for (int i = 0; i < len; i++) {
        notebook_ptr[i] = arg[i];
    }
    notebook_ptr[len] = '\0';
    notebook_length = (uint32_t)len;
    print("\nNotebook updated.");
}

static void command_read() {
    if (notebook_length == 0 || notebook_ptr == 0) {
        print("\nNotebook is empty.");
        return;
    }
    print("\nContent: ");
    print(notebook_ptr);
}

static void command_free() {
    if (notebook_ptr != 0) {
        kfree(notebook_ptr);
        notebook_ptr = 0;
    }
    notebook_length = 0;
    print("\nNotebook cleared.");
}

static void command_version() {
    print("\n[OS-Kernel] v0.0.x (64-bit Long Mode, C++)");
    print("\nBIOS stage1/stage2 + FAT12 loader + IDT/IRQ");
}

static void command_atatest() {
    uint8_t buffer[512];
    if (!ata.read_sector(0, buffer)) {
        print("\nATA read failed.");
        return;
    }

    print("\nSector 0:");
    for (int i = 0; i < 16; i++) {
        print(" ");
        print_hex32(buffer[i]);
    }
}

static void command_load(char* arg) {
    if (arg == 0) {
        print("\nUsage: load [filename]");
        return;
    }

    char name83[11];
    to_name83(arg, name83);

    DirEntry entry;
    if (!fat.find_file(name83, &entry)) {
        print("\nFile not found.");
        return;
    }

    uint32_t buffer_size = entry.file_size + 1;
    if (buffer_size < 512) {
        buffer_size = 512;
    }

    uint8_t* file_buffer = (uint8_t*)kmalloc(buffer_size);
    if (file_buffer == 0) {
        print("\nOut of memory.");
        return;
    }

    int bytes_read = fat.read_file(&entry, file_buffer);
    if (bytes_read < 0) {
        print("\nFailed to read file.");
        kfree(file_buffer);
        return;
    }

    file_buffer[entry.file_size] = '\0';
    print("\n");
    print((const char*)file_buffer);
    kfree(file_buffer);
}

static void command_save(char* arg) {
    if (arg == 0) {
        print("\nUsage: save [filename]");
        return;
    }
    if (notebook_length == 0) {
        print("\nNotebook is empty. Use write first.");
        return;
    }

    char name83[11];
    to_name83(arg, name83);

    if (fat.write_file(name83, (uint8_t*)notebook_ptr, notebook_length)) {
        print("\nSaved: ");
        print(arg);
    } else {
        print("\nFailed to save.");
    }
}

static void command_rm(char* arg) {
    if (arg == 0) {
        print("\nUsage: rm [filename]");
        return;
    }

    char name83[11];
    to_name83(arg, name83);

    if (fat.delete_file(name83)) {
        print("\nDeleted: ");
        print(arg);
    } else {
        print("\nFile not found.");
    }
}

static void command_uptime() {
    print("\nTick: ");
    print_hex32(pit.get_tick());
    print("\nTSC delta: ");
    print_hex64(read_tsc() - boot_tsc);
}

static void execute_command() {
    shell_buffer[buffer_index] = '\0';
    save_history();

    char* arg = get_argument(shell_buffer);
    char cmd[32];
    int i = 0;
    while (shell_buffer[i] != ' ' && shell_buffer[i] != '\0' && i < 31) {
        cmd[i] = to_lower_ascii(shell_buffer[i]);
        i++;
    }
    cmd[i] = '\0';

    if (strcmp64(cmd, "help") == 0) {
        command_help();
    } else if (strcmp64(cmd, "clear") == 0) {
        terminal.clear();
        serial_putchar('\r');
        serial_putchar('\n');
    } else if (strcmp64(cmd, "version") == 0) {
        command_version();
    } else if (strcmp64(cmd, "bootinfo") == 0) {
        print_boot_info();
    } else if (strcmp64(cmd, "memmap") == 0) {
        print_memmap();
    } else if (strcmp64(cmd, "memstat") == 0) {
        command_memstat();
    } else if (strcmp64(cmd, "echo") == 0) {
        command_echo(arg);
    } else if (strcmp64(cmd, "write") == 0) {
        command_write(arg);
    } else if (strcmp64(cmd, "read") == 0) {
        command_read();
    } else if (strcmp64(cmd, "free") == 0) {
        command_free();
    } else if (strcmp64(cmd, "dump") == 0) {
        dump_state();
    } else if (strcmp64(cmd, "atatest") == 0) {
        command_atatest();
    } else if (strcmp64(cmd, "ls") == 0) {
        fat.list_files();
    } else if (strcmp64(cmd, "load") == 0) {
        command_load(arg);
    } else if (strcmp64(cmd, "save") == 0) {
        command_save(arg);
    } else if (strcmp64(cmd, "rm") == 0) {
        command_rm(arg);
    } else if (strcmp64(cmd, "pagefault") == 0) {
        volatile uint32_t* bad_ptr = (uint32_t*)0x80000000;
        *bad_ptr = 0x1234;
    } else if (strcmp64(cmd, "uptime") == 0) {
        command_uptime();
    } else if (buffer_index > 0) {
        print("\nUnknown command: ");
        print(cmd);
    }

    buffer_index = 0;
    shell_buffer[0] = '\0';
    print("\n" PROMPT);
}

extern "C" void shell_input(char ascii) {
    if (ascii == '\n') {
        execute_command();
    } else if (ascii == '\b') {
        if (buffer_index > 0) {
            buffer_index--;
            shell_buffer[buffer_index] = '\0';
            terminal.putchar('\b');
            serial_putchar('\b');
            serial_putchar(' ');
            serial_putchar('\b');
        }
    } else if (ascii >= 32 && ascii <= 126) {
        if (buffer_index < MAX_BUFFER_SIZE - 1) {
            shell_buffer[buffer_index++] = ascii;
            shell_buffer[buffer_index] = '\0';
            terminal.putchar(ascii);
            serial_putchar(ascii);
        }
    }
}

extern "C" void keyboard_handler64() {
    keyboard.handle();
}

extern "C" void timer_handler64() {
    pit.handle();
}

extern "C" void kernel64_main(const BootInfo* boot_info) {
    serial_init();
    terminal.clear();

    g_boot_info = boot_info;
    boot_tsc = read_tsc();

    print("Long mode OK\n");
    if (g_boot_info != 0 && g_boot_info->magic == BOOT_INFO_MAGIC) {
        print("BootInfo magic: ");
        print_hex32(g_boot_info->magic);
        print("\nKernel load: ");
        print_hex32(g_boot_info->kernel_load_addr);
        print("\nKernel sectors: ");
        print_hex32(g_boot_info->kernel_sector_count);
        print("\nKernel bytes: ");
        print_hex32(g_boot_info->kernel_file_size);
        print("\nE820 entries: ");
        print_hex32(g_boot_info->memory_map_entry_count);
        print("\n");
    } else {
        print("BootInfo invalid\n");
    }

    pmm64_init(boot_info);
    heap_init();
    ata.init();
    fat.init();
    idt64_init();
    keyboard.init();
    pit.init();
    __asm__ volatile("sti");

    print("Memory ready\n");
    print("Interrupts ready\n");
    print(PROMPT);

    while (1) {
        __asm__ volatile("hlt");
    }
}
