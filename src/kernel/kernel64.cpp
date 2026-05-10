#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "arch/x86/io.h"
}

#include "drivers/terminal.h"
#include "drivers/ata.h"
#include "fs/fat12.h"
#include "kernel/boot_info.h"

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY 10
#define MAX_CMD_LEN 80
#define NOTEBOOK_CAPACITY 4096
#define FILE_BUFFER_CAPACITY 4096
#define PROMPT "OS64> "

Terminal terminal;
ATADriver ata;
FAT12Driver fat(&ata);

static const BootInfo* g_boot_info = 0;
static char shell_buffer[MAX_BUFFER_SIZE];
static int buffer_index = 0;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_index = 0;
static char notebook[NOTEBOOK_CAPACITY];
static uint32_t notebook_length = 0;
static uint32_t boot_tsc_low = 0;

inline void* operator new(size_t, void* ptr) { return ptr; }
inline void* operator new[](size_t, void* ptr) { return ptr; }

static void* kernel64_alloc_fail() {
    while (1) {
    }
}

void* operator new(size_t) { return kernel64_alloc_fail(); }
void* operator new[](size_t) { return kernel64_alloc_fail(); }
void operator delete(void*) {}
void operator delete[](void*) {}
void operator delete(void*, size_t) {}
void operator delete[](void*, size_t) {}

extern "C" void __cxa_pure_virtual() {
    while (1) {
    }
}

static const char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char kbd_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

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

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
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

static void serial_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(str[i]);
    }
}

static void print(const char* str) {
    terminal.print(str);
    serial_print(str);
}

static void print_hex(uint32_t value) {
    terminal.print_hex(value);

    char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buffer[i] = hex_chars[value & 0x0F];
        value >>= 4;
    }
    buffer[10] = '\0';
    serial_print(buffer);
}

static uint32_t read_tsc_low() {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo;
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

static void print_e820_entry(const E820Entry* entry, uint32_t index) {
    print("\n[");
    print_hex(index);
    print("] base=");
    print_hex(entry->base_high);
    print_hex(entry->base_low);
    print(" len=");
    print_hex(entry->length_high);
    print_hex(entry->length_low);
    print(" type=");
    print_hex(entry->type);
}

static void print_boot_info() {
    if (g_boot_info == 0) {
        print("\nBootInfo: null");
        return;
    }

    print("\nBootInfo magic: ");
    print_hex(g_boot_info->magic);
    print("\nVersion: ");
    print_hex(g_boot_info->version);
    print("\nBoot drive: ");
    print_hex(g_boot_info->boot_drive);
    print("\nKernel load: ");
    print_hex(g_boot_info->kernel_load_addr);
    print("\nKernel sectors: ");
    print_hex(g_boot_info->kernel_sector_count);
    print("\nKernel bytes: ");
    print_hex(g_boot_info->kernel_file_size);
    print("\nStage2 load: ");
    print_hex(g_boot_info->stage2_load_addr);
    print("\nMemory map addr: ");
    print_hex(g_boot_info->memory_map_addr);
    print("\nMemory map entries: ");
    print_hex(g_boot_info->memory_map_entry_count);
    print("\nMemory map entry size: ");
    print_hex(g_boot_info->memory_map_entry_size);
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
    print_hex((uint32_t)(uintptr_t)g_boot_info);
    print("\nInput len: ");
    print_hex((uint32_t)buffer_index);
    print("\nHistory count: ");
    print_hex((uint32_t)history_count);
    print("\nNotebook bytes: ");
    print_hex(notebook_length);
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
        serial_print("\b \b");
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
    print("\nAvailable commands: help, clear, version, bootinfo, memmap, echo, write, read");
    print("\nfree, dump, atatest, ls, load, save, rm, pagefault, uptime");
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

    for (int i = 0; i < len; i++) {
        notebook[i] = arg[i];
    }
    notebook[len] = '\0';
    notebook_length = (uint32_t)len;
    print("\nNotebook updated.");
}

static void command_read() {
    if (notebook_length == 0) {
        print("\nNotebook is empty.");
        return;
    }
    print("\nContent: ");
    print(notebook);
}

static void command_free() {
    notebook[0] = '\0';
    notebook_length = 0;
    print("\nNotebook cleared.");
}

static void command_version() {
    print("\n[OS-Kernel] v0.0.x (64-bit Long Mode, C++)");
    print("\nBIOS stage1/stage2 + FAT12 loader");
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
        print_hex(buffer[i]);
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

    if (entry.file_size >= FILE_BUFFER_CAPACITY) {
        print("\nFile too large for OS64 buffer.");
        return;
    }

    static uint8_t file_buffer[FILE_BUFFER_CAPACITY];
    int bytes_read = fat.read_file(&entry, file_buffer);
    if (bytes_read < 0) {
        print("\nFailed to read file.");
        return;
    }

    file_buffer[entry.file_size] = '\0';
    print("\n");
    print((const char*)file_buffer);
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

    if (fat.write_file(name83, (uint8_t*)notebook, notebook_length)) {
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
    uint32_t delta = read_tsc_low() - boot_tsc_low;
    print("\nTSC delta(low32): ");
    print_hex(delta);
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
        serial_print("\n");
    } else if (strcmp64(cmd, "version") == 0) {
        command_version();
    } else if (strcmp64(cmd, "bootinfo") == 0) {
        print_boot_info();
    } else if (strcmp64(cmd, "memmap") == 0) {
        print_memmap();
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
            serial_print("\b \b");
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

static void poll_keyboard() {
    static int extended = 0;
    static int left_shift = 0;
    static int right_shift = 0;
    static int caps_lock = 0;

    if ((inb(0x64) & 1) == 0) {
        return;
    }

    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    if (extended) {
        extended = 0;
        if (scancode == 0x48) {
            shell_recall_history(-1);
        } else if (scancode == 0x50) {
            shell_recall_history(1);
        }
        return;
    }

    if (scancode == 0x2A) {
        left_shift = 1;
        return;
    }
    if (scancode == 0x36) {
        right_shift = 1;
        return;
    }
    if (scancode == 0xAA) {
        left_shift = 0;
        return;
    }
    if (scancode == 0xB6) {
        right_shift = 0;
        return;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }
    if (scancode & 0x80) {
        return;
    }

    int shifted = left_shift || right_shift;
    char ascii = shifted ? kbd_us_shift[scancode] : kbd_us[scancode];
    if (ascii == 0) {
        return;
    }

    if (caps_lock && is_alpha(ascii)) {
        if (ascii >= 'a' && ascii <= 'z') {
            ascii = to_upper_ascii(ascii);
        } else {
            ascii = to_lower_ascii(ascii);
        }
    }

    shell_input(ascii);
}

extern "C" void kernel64_main(const BootInfo* boot_info) {
    serial_init();
    terminal.clear();

    g_boot_info = boot_info;
    boot_tsc_low = read_tsc_low();

    print("Long mode OK\n");
    if (g_boot_info != 0 && g_boot_info->magic == BOOT_INFO_MAGIC) {
        print("BootInfo magic: ");
        print_hex(g_boot_info->magic);
        print("\nKernel load: ");
        print_hex(g_boot_info->kernel_load_addr);
        print("\nKernel sectors: ");
        print_hex(g_boot_info->kernel_sector_count);
        print("\nKernel bytes: ");
        print_hex(g_boot_info->kernel_file_size);
        print("\nE820 entries: ");
        print_hex(g_boot_info->memory_map_entry_count);
        print("\n");
    } else {
        print("BootInfo invalid\n");
    }

    ata.init();
    fat.init();

    print("Keyboard polling ready\n");
    print(PROMPT);

    while (1) {
        poll_keyboard();
    }
}
