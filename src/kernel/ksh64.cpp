#include <stdint.h>
#include <stddef.h>

extern "C" {
    #include "heap.h"
}

#include "arch/x86/paging64.h"
#include "arch/x86/pmm64.h"
#include "drivers/terminal.h"
#include "fs/vfs.h"
#include "kernel/driver/driver_manager.h"
#include "kernel/kernel_diag.h"
#include "kernel/ksh64.h"
#include "kernel/kutil64.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall64.h"

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY 10
#define MAX_CMD_LEN 80
#define NOTEBOOK_CAPACITY 32768

static char shell_buffer[MAX_BUFFER_SIZE];
static int buffer_index = 0;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_index = history_count;
static char* notebook_ptr = 0;
static uint32_t notebook_length = 0;

const char* kernel_shell_prompt() {
    return PROMPT;
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

static uint64_t e820_base(const E820Entry* entry) {
    return ((uint64_t)entry->base_high << 32) | entry->base_low;
}

static uint64_t e820_length(const E820Entry* entry) {
    return ((uint64_t)entry->length_high << 32) | entry->length_low;
}

static int ends_with_ci(const char* text, const char* suffix) {
    int text_len = strlen64(text);
    int suffix_len = strlen64(suffix);
    if (text_len < suffix_len) {
        return 0;
    }
    const char* tail = text + text_len - suffix_len;
    for (int i = 0; i < suffix_len; i++) {
        if (to_lower_ascii(tail[i]) != to_lower_ascii(suffix[i])) {
            return 0;
        }
    }
    return 1;
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

static void print_memmap() {
    const BootInfo* boot_info = kernel_boot_info();
    if (boot_info == 0) {
        print("\nNo BootInfo.");
        return;
    }

    const E820Entry* entries = (const E820Entry*)(uintptr_t)boot_info->memory_map_addr;
    for (uint32_t i = 0; i < boot_info->memory_map_entry_count; i++) {
        print_e820_entry(&entries[i], i);
    }
}

static void dump_state() {
    print("\n=== STATE DUMP ===");
    print("\nUser tests: ");
    print_hex32(kernel_user_test_count());
    print("\nSyscalls: ");
    print_hex32(kernel_syscall_count());
    print("\nNext PID: ");
    print_hex32(next_pid);
    print("\nCurrent process: ");
    print_process_summary(current_process(), pit.get_tick());
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
    print("\nSched queue count: ");
    print_hex32(sched_queue_count);
    print("\nSched last pid: ");
    print_hex32(sched_last_pid);
    print("\nSched switches: ");
    print_hex32(sched_switch_count);
    print("\nSched yields: ");
    print_hex32(sched_yield_count);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        print("\nProcess slot ");
        print_hex32(i);
        print(": ");
        print_process_summary(&process_table[i], pit.get_tick());
    }
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
    print("\nfree, dump, sched, drivers, drvinfo [path], drvcheck [path], drvload [path], drvautoload [dir]");
    print("\nmounts, atatest, ls [path], load, save, rm, mkdir, rmdir, pagefault, uptime");
    print("\nrun, resume, usertest, ushell, ushellc");
}

static void command_sched() {
    print_scheduler_info(sched_queue,
                         sched_queue_count,
                         sched_queue_head,
                         SCHED_QUEUE_SIZE,
                         sched_last_pid,
                         sched_switch_count,
                         sched_yield_count,
                         pit.get_tick());
}

static void command_mounts() {
    print_vfs_mounts();
}

static void command_ls(char* arg) {
    if (arg == 0 || arg[0] == '\0') {
        vfs_list_files();
        return;
    }
    vfs_list_files_at(arg);
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

    VFSFileInfo file_info;
    if (vfs_get_file_info(arg, &file_info) != VFS_OK) {
        print("\nFile not found.");
        return;
    }

    uint32_t buffer_size = file_info.size + 1;
    if (buffer_size < 512) {
        buffer_size = 512;
    }

    uint8_t* file_buffer = (uint8_t*)kmalloc(buffer_size);
    if (file_buffer == 0) {
        print("\nOut of memory.");
        return;
    }

    uint32_t bytes_read = 0;
    if (vfs_read_file(arg, file_buffer, buffer_size, &bytes_read) != VFS_OK) {
        print("\nFailed to read file.");
        kfree(file_buffer);
        return;
    }

    file_buffer[bytes_read] = '\0';
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

    if (vfs_write_file(arg, (uint8_t*)notebook_ptr, notebook_length) == VFS_OK) {
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

    if (vfs_delete_file(arg) == VFS_OK) {
        print("\nDeleted: ");
        print(arg);
    } else {
        print("\nFile not found.");
    }
}

static void command_mkdir(char* arg) {
    if (arg == 0) {
        print("\nUsage: mkdir [path]");
        return;
    }

    if (vfs_mkdir(arg) == VFS_OK) {
        print("\nCreated dir: ");
        print(arg);
    } else {
        print("\nFailed to create dir.");
    }
}

static void command_rmdir(char* arg) {
    if (arg == 0) {
        print("\nUsage: rmdir [path]");
        return;
    }

    if (vfs_rmdir(arg) == VFS_OK) {
        print("\nRemoved dir: ");
        print(arg);
    } else {
        print("\nFailed to remove dir.");
    }
}

static void command_run(char* arg) {
    if (arg == 0 || arg[0] == '\0') {
        print("\nUsage: run [filename]");
        return;
    }
    if (ends_with_ci(arg, ".drv")) {
        print("\nDRV packages are kernel drivers. Use: drvload ");
        print(arg);
        return;
    }
    run_user_program(arg);
}

static void command_usertest() {
    char default_program[] = "utest.bin";
    command_run(default_program);
}

static void command_ushell() {
    char default_program[] = "ushell_c.elf";
    command_run(default_program);
}

static void command_ushellc() {
    char default_program[] = "ushell_c.elf";
    command_run(default_program);
}

static void command_resume() {
    resume_user_program(0);
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
    } else if (strcmp64(cmd, "sched") == 0) {
        command_sched();
    } else if (strcmp64(cmd, "drivers") == 0) {
        command_drivers();
    } else if (strcmp64(cmd, "drvinfo") == 0) {
        command_drvinfo(arg);
    } else if (strcmp64(cmd, "drvcheck") == 0) {
        command_drvcheck(arg);
    } else if (strcmp64(cmd, "drvload") == 0) {
        command_drvload(arg);
    } else if (strcmp64(cmd, "drvautoload") == 0) {
        command_drvautoload(arg);
    } else if (strcmp64(cmd, "mounts") == 0) {
        command_mounts();
    } else if (strcmp64(cmd, "atatest") == 0) {
        command_atatest();
    } else if (strcmp64(cmd, "ls") == 0) {
        command_ls(arg);
    } else if (strcmp64(cmd, "load") == 0) {
        command_load(arg);
    } else if (strcmp64(cmd, "save") == 0) {
        command_save(arg);
    } else if (strcmp64(cmd, "rm") == 0) {
        command_rm(arg);
    } else if (strcmp64(cmd, "mkdir") == 0) {
        command_mkdir(arg);
    } else if (strcmp64(cmd, "rmdir") == 0) {
        command_rmdir(arg);
    } else if (strcmp64(cmd, "run") == 0) {
        command_run(arg);
    } else if (strcmp64(cmd, "resume") == 0) {
        command_resume();
    } else if (strcmp64(cmd, "pagefault") == 0) {
        volatile uint32_t* bad_ptr = (uint32_t*)0x80000000;
        *bad_ptr = 0x1234;
    } else if (strcmp64(cmd, "usertest") == 0) {
        command_usertest();
    } else if (strcmp64(cmd, "ushell") == 0) {
        command_ushell();
    } else if (strcmp64(cmd, "ushellc") == 0) {
        command_ushellc();
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
