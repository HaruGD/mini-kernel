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
#include "fs/fat12.h"
#include "kernel/boot_info.h"

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY 10
#define MAX_CMD_LEN 80
#define NOTEBOOK_CAPACITY 32768
#define PROMPT "OS64> "
#define USER_PROGRAM_SLOT_COUNT 2
#define USER_SLOT0_CODE_BASE  0x0000000009000000ULL
#define USER_SLOT0_STACK_BASE 0x0000000009100000ULL
#define USER_SLOT1_CODE_BASE  0x0000000009200000ULL
#define USER_SLOT1_STACK_BASE 0x0000000009300000ULL
#define SYSCALL_RETURN_TO_KERNEL 0xFFFFFFFFFFFFFFFFULL
#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_PUTCHAR 3
#define SYS_GETCHAR 4
#define SYS_LIST_FILES 5
#define SYS_CAT_FILE 6
#define SYS_RUN_USER 7
#define SYS_VERSION 8
#define SYS_BOOTINFO 9
#define SYS_MEMSTAT 10
#define SYS_RM_FILE 11
#define SYS_UPTIME 12

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
static uint32_t user_test_count = 0;
static uint32_t syscall_count = 0;
static volatile int user_input_mode = 0;
static uint32_t user_program_depth = 0;
extern "C" void enter_user_mode(uint64_t rip, uint64_t rsp);

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

static uint32_t parse_uint32(const char* text) {
    uint32_t value = 0;
    int i = 0;
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (uint32_t)(text[i] - '0');
        i++;
    }
    return value;
}

static uint64_t current_rsp() {
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static void print_n(const char* str, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        putchar_both(str[i]);
    }
}

static int copy_user_cstring(const char* user_ptr, char* kernel_buf, uint32_t max_len) {
    if (user_ptr == 0 || kernel_buf == 0 || max_len == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < max_len - 1; i++) {
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + i)) == 0) {
            return 0;
        }

        char c = user_ptr[i];
        kernel_buf[i] = c;
        if (c == '\0') {
            return 1;
        }
    }

    kernel_buf[max_len - 1] = '\0';
    return 1;
}

static void user_input_reset() {
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
    print("\nUser tests: ");
    print_hex32(user_test_count);
    print("\nSyscalls: ");
    print_hex32(syscall_count);
    print("\nNotebook bytes: ");
    print_hex32(notebook_length);
    print("\nPMM free pages: ");
    print_hex32(pmm64_get_free_block_count());
    print("\nPaging root: ");
    print_hex64(paging64_get_root_phys());
    print("\nHeap used bytes: ");
    print_hex64(heap_total_used());
    print("\nHeap free bytes: ");
    print_hex64(heap_total_free());
    print("\nHeap mapped bytes: ");
    print_hex64(heap_total_mapped_bytes());
    print("\nHeap mapped pages: ");
    print_hex32(heap_mapped_page_count());
    print("\nHeap regions: ");
    print_hex32(heap_region_count());
    print("\nNotebook ptr: ");
    print_hex64((uint64_t)(uintptr_t)notebook_ptr);
    if (notebook_ptr != 0) {
        print("\nNotebook phys: ");
        print_hex64(paging64_get_phys((uint64_t)(uintptr_t)notebook_ptr));
    }
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
    print("\nAvailable commands: help, clear, version, bootinfo, memmap, memstat, echo, write, read, fill");
    print("\nfree, dump, atatest, ls, load, save, rm, pagefault, uptime");
    print("\nrun, usertest, ushell");
}

static void command_memstat() {
    print("\nPMM total pages: ");
    print_hex32(pmm64_get_total_block_count());
    print("\nPMM free pages: ");
    print_hex32(pmm64_get_free_block_count());
    print("\nPaging root: ");
    print_hex64(paging64_get_root_phys());
    print("\nHeap used bytes: ");
    print_hex64(heap_total_used());
    print("\nHeap free bytes: ");
    print_hex64(heap_total_free());
    print("\nHeap mapped bytes: ");
    print_hex64(heap_total_mapped_bytes());
    print("\nHeap mapped pages: ");
    print_hex32(heap_mapped_page_count());
    print("\nHeap regions: ");
    print_hex32(heap_region_count());
}

static void command_echo(char* arg) {
    if (arg == 0) {
        print("\nUsage: echo [text]");
        return;
    }
    print("\n");
    print(arg);
}

static int run_user_program(const char* filename) {
    if (filename == 0 || filename[0] == '\0') {
        print("\nUser program filename is empty.");
        return 0;
    }

    if (user_program_depth >= USER_PROGRAM_SLOT_COUNT) {
        print("\nUser program nesting limit reached.");
        return 0;
    }

    uint64_t user_code_base;
    uint64_t user_stack_base;
    if (user_program_depth == 0) {
        user_code_base = USER_SLOT0_CODE_BASE;
        user_stack_base = USER_SLOT0_STACK_BASE;
    } else {
        user_code_base = USER_SLOT1_CODE_BASE;
        user_stack_base = USER_SLOT1_STACK_BASE;
    }
    uint64_t user_stack_top = user_stack_base + PAGING64_PAGE_SIZE;

    char user_name83[11];
    to_name83(filename, user_name83);

    DirEntry entry;
    if (!fat.find_file(user_name83, &entry)) {
        print("\nUser program not found: ");
        print(filename);
        return 0;
    }

    if (entry.file_size == 0 || entry.file_size > PAGING64_PAGE_SIZE) {
        print("\nUser program size is invalid for the current loader.");
        return 0;
    }

    uint32_t program_buffer_size = entry.file_size;
    if (program_buffer_size < 512) {
        program_buffer_size = 512;
    }

    uint8_t* program_buffer = (uint8_t*)kmalloc(program_buffer_size);
    if (program_buffer == 0) {
        print("\nOut of memory for user program.");
        return 0;
    }

    if (fat.read_file(&entry, program_buffer) < 0) {
        print("\nFailed to read user program: ");
        print(filename);
        kfree(program_buffer);
        return 0;
    }

    uint64_t code_phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
    uint64_t stack_phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
    if (code_phys == 0 || stack_phys == 0) {
        if (code_phys != 0) {
            pmm64_free_block((void*)(uintptr_t)code_phys);
        }
        if (stack_phys != 0) {
            pmm64_free_block((void*)(uintptr_t)stack_phys);
        }
        kfree(program_buffer);
        print("\nFailed to allocate user program pages.");
        return 0;
    }

    if (!paging64_map_page(user_code_base, code_phys, PAGING64_FLAG_WRITABLE | PAGING64_FLAG_USER)) {
        pmm64_free_block((void*)(uintptr_t)code_phys);
        pmm64_free_block((void*)(uintptr_t)stack_phys);
        kfree(program_buffer);
        print("\nFailed to map user code page.");
        return 0;
    }

    if (!paging64_map_page(user_stack_base, stack_phys, PAGING64_FLAG_WRITABLE | PAGING64_FLAG_USER)) {
        paging64_unmap_page(user_code_base);
        pmm64_free_block((void*)(uintptr_t)code_phys);
        pmm64_free_block((void*)(uintptr_t)stack_phys);
        kfree(program_buffer);
        print("\nFailed to map user stack page.");
        return 0;
    }

    for (uint32_t i = 0; i < entry.file_size; i++) {
        *((volatile uint8_t*)(uintptr_t)(user_code_base + i)) = program_buffer[i];
    }
    kfree(program_buffer);

    uint64_t saved_rsp0 = gdt64_get_kernel_stack();
    uint8_t saved_pic1_mask = inb(0x21);
    int saved_user_input_mode = user_input_mode;
    print("\nRunning user program: ");
    print(filename);
    print("\n");
    user_input_reset();
    user_input_mode = 1;
    outb(0x21, saved_pic1_mask | 0x02);
    gdt64_set_kernel_stack(current_rsp() - 8);
    user_program_depth++;
    enter_user_mode(user_code_base, user_stack_top - 16);
    user_program_depth--;

    outb(0x21, saved_pic1_mask);
    user_input_mode = saved_user_input_mode;
    user_input_reset();
    gdt64_set_kernel_stack(saved_rsp0);

    paging64_unmap_page(user_code_base);
    paging64_unmap_page(user_stack_base);
    pmm64_free_block((void*)(uintptr_t)code_phys);
    pmm64_free_block((void*)(uintptr_t)stack_phys);

    print("\nReturned from user program.\n");
    return 1;
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

static void command_fill(char* arg) {
    if (arg == 0) {
        print("\nUsage: fill [bytes] [char]");
        return;
    }

    char* second = get_argument(arg);
    if (second == 0 || second[0] == '\0') {
        print("\nUsage: fill [bytes] [char]");
        return;
    }

    uint32_t len = parse_uint32(arg);
    if (len == 0) {
        print("\nSize must be > 0.");
        return;
    }
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

    for (uint32_t i = 0; i < len; i++) {
        notebook_ptr[i] = second[0];
    }
    notebook_ptr[len] = '\0';
    notebook_length = len;
    print("\nNotebook filled: ");
    print_hex32(len);
    print(" bytes");
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

static void command_run(char* arg) {
    if (arg == 0 || arg[0] == '\0') {
        print("\nUsage: run [filename]");
        return;
    }
    run_user_program(arg);
}

static void command_usertest() {
    char default_program[] = "UTEST.BIN";
    command_run(default_program);
}

static void command_ushell() {
    char default_program[] = "USHELL.BIN";
    command_run(default_program);
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
    } else if (strcmp64(cmd, "fill") == 0) {
        command_fill(arg);
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
    } else if (strcmp64(cmd, "run") == 0) {
        command_run(arg);
    } else if (strcmp64(cmd, "pagefault") == 0) {
        volatile uint32_t* bad_ptr = (uint32_t*)0x80000000;
        *bad_ptr = 0x1234;
    } else if (strcmp64(cmd, "usertest") == 0) {
        command_usertest();
    } else if (strcmp64(cmd, "ushell") == 0) {
        command_ushell();
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

extern "C" int user_input_active64() {
    return user_input_mode;
}

extern "C" void keyboard_deliver_char64(char ascii) {
    if (user_input_mode) {
        return;
    }

    shell_input(ascii);
}

extern "C" void timer_handler64() {
    pit.handle();
}

extern "C" void user_test_interrupt_handler64() {
    user_test_count++;
    print("\nUser mode reached.");
}

extern "C" void user_exit_interrupt_handler64() {
    print("\nUser mode exit requested.");
}

extern "C" uint64_t syscall_dispatch64(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t) {
    syscall_count++;

    if (syscall_no == SYS_WRITE) {
        const char* user_ptr = (const char*)(uintptr_t)arg1;
        uint64_t length = arg2;
        if (length == 0) {
            return 0;
        }
        if (length > 4096) {
            length = 4096;
        }

        if (paging64_get_phys((uint64_t)(uintptr_t)user_ptr) == 0) {
            return (uint64_t)-1;
        }
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + length - 1)) == 0) {
            return (uint64_t)-1;
        }

        print_n(user_ptr, length);
        return length;
    }

    if (syscall_no == SYS_EXIT) {
        print("\nUser mode exit requested.");
        return SYSCALL_RETURN_TO_KERNEL;
    }

    if (syscall_no == SYS_PUTCHAR) {
        putchar_both((char)(arg1 & 0xFF));
        return 1;
    }

    if (syscall_no == SYS_GETCHAR) {
        return (uint64_t)(unsigned char)keyboard.read_char_blocking();
    }

    if (syscall_no == SYS_LIST_FILES) {
        fat.list_files();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_CAT_FILE) {
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user filename pointer.");
            return (uint64_t)-1;
        }

        char name83[11];
        to_name83(file_name, name83);

        DirEntry entry;
        if (!fat.find_file(name83, &entry)) {
            print("\nFile not found: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }

        uint32_t buffer_size = entry.file_size + 1;
        if (buffer_size < 512) {
            buffer_size = 512;
        }

        uint8_t* file_buffer = (uint8_t*)kmalloc(buffer_size);
        if (file_buffer == 0) {
            print("\nOut of memory reading file.\n");
            return (uint64_t)-1;
        }

        int bytes_read = fat.read_file(&entry, file_buffer);
        if (bytes_read < 0) {
            kfree(file_buffer);
            print("\nFailed to read file: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }

        file_buffer[entry.file_size] = '\0';
        print((const char*)file_buffer);
        if (entry.file_size == 0 || file_buffer[entry.file_size - 1] != '\n') {
            print("\n");
        }
        kfree(file_buffer);
        return entry.file_size;
    }

    if (syscall_no == SYS_RUN_USER) {
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user program pointer.");
            return (uint64_t)-1;
        }

        if (!run_user_program(file_name)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_VERSION) {
        command_version();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_BOOTINFO) {
        print_boot_info();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_MEMSTAT) {
        command_memstat();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_RM_FILE) {
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user filename pointer.");
            return (uint64_t)-1;
        }

        char name83[11];
        to_name83(file_name, name83);

        if (fat.delete_file(name83)) {
            print("\nDeleted: ");
            print(file_name);
            print("\n");
            return 0;
        }

        print("\nFile not found: ");
        print(file_name);
        print("\n");
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_UPTIME) {
        command_uptime();
        print("\n");
        return 0;
    }

    print("\nUnknown syscall: ");
    print_hex32((uint32_t)syscall_no);
    return (uint64_t)-1;
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
    paging64_init();
    heap_init();
    gdt64_init();
    ata.init();
    fat.init();
    idt64_init();
    keyboard.init();
    pit.init();
    __asm__ volatile("sti");

    print("Memory ready\n");
    print("GDT/TSS ready\n");
    print("Interrupts ready\n");
    print(PROMPT);

    while (1) {
        __asm__ volatile("hlt");
    }
}
