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
#include "fs/vfs.h"
#include "kernel/boot_info.h"
#include "kernel/elf64.h"
#include "kernel/process.h"

#define MAX_BUFFER_SIZE 256
#define MAX_HISTORY 10
#define MAX_CMD_LEN 80
#define NOTEBOOK_CAPACITY 32768
#define PROMPT "OS64> "
#define USER_PROGRAM_SLOT_COUNT 4
#define PROCESS_TABLE_SIZE 8
#define USER_SLOT0_CODE_BASE  0x0000000009000000ULL
#define USER_SLOT0_STACK_BASE 0x0000000009100000ULL
#define USER_SLOT1_CODE_BASE  0x0000000009200000ULL
#define USER_SLOT1_STACK_BASE 0x0000000009300000ULL
#define USER_SLOT2_CODE_BASE  0x0000000009400000ULL
#define USER_SLOT2_STACK_BASE 0x0000000009500000ULL
#define USER_SLOT3_CODE_BASE  0x0000000009600000ULL
#define USER_SLOT3_STACK_BASE 0x0000000009700000ULL
#define SYSCALL_RETURN_TO_KERNEL 0xFFFFFFFFFFFFFFFEULL
#define SYSCALL_YIELD_TO_KERNEL  0xFFFFFFFFFFFFFFFDULL
#define TIMER_PREEMPT_TO_KERNEL  0xFFFFFFFFFFFFFFFCULL
#define SYSCALL_SLEEP_TO_KERNEL  0xFFFFFFFFFFFFFFFBULL
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
#define SYS_TOUCH_FILE 13
#define SYS_SAVE_FILE 14
#define SYS_GET_PID 15
#define SYS_GET_PPID 16
#define SYS_PS 17
#define SYS_LAST_STATUS 18
#define SYS_WAIT_CHILD 19
#define SYS_SCHED_INFO 20
#define SYS_YIELD 21
#define SYS_RESUME_USER 22
#define SYS_KILL_USER 23
#define SYS_REAP_ALL_CHILDREN 24
#define SYS_JOBS 25
#define SYS_SLEEP 26
#define SYS_SET_BACKGROUND 27
#define SYS_CHILDREN_ACTIVE 28
#define SYS_REAP_ALL_CHILDREN_SILENT 29
#define SYS_RM_FILE_SILENT 30
#define SYS_TOUCH_FILE_SILENT 31
#define SYS_SAVE_FILE_SILENT 32
#define SYS_VFS_MOUNTS 33
#define SYS_LIST_FILES_AT 34
#define SYS_VFS_OPEN 35
#define SYS_VFS_READ 36
#define SYS_VFS_WRITE 37
#define SYS_VFS_CLOSE 38
#define SCHED_QUEUE_SIZE PROCESS_TABLE_SIZE
#define SCHED_DEFAULT_TIMESLICE 6

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
static uint32_t next_pid = 1;
static Process process_table[PROCESS_TABLE_SIZE];
static Process* process_stack[USER_PROGRAM_SLOT_COUNT];
static Process* sched_queue[SCHED_QUEUE_SIZE];
static uint32_t sched_queue_count = 0;
static uint32_t sched_queue_head = 0;
static uint32_t sched_last_pid = 0;
static uint32_t sched_switch_count = 0;
static uint32_t sched_yield_count = 0;

struct UserLaunchInfo {
    uint32_t argc;
    char* argv[PROCESS_ARG_MAX];
};
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

static void copy_string64(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static int is_space64(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
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

static uint32_t parse_launch_command(char* command_line, UserLaunchInfo* launch) {
    if (command_line == 0 || launch == 0) {
        return 0;
    }

    launch->argc = 0;
    for (uint32_t i = 0; i < PROCESS_ARG_MAX; i++) {
        launch->argv[i] = 0;
    }

    char* cursor = command_line;
    while (*cursor != '\0') {
        while (is_space64(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (launch->argc >= PROCESS_ARG_MAX) {
            break;
        }

        launch->argv[launch->argc++] = cursor;
        while (*cursor != '\0' && !is_space64(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        *cursor++ = '\0';
    }

    return launch->argc;
}

static uint64_t prepare_user_stack_with_argv(Process* process, uint64_t user_stack_top, const UserLaunchInfo* launch) {
    if (process == 0 || launch == 0) {
        return user_stack_top - 16;
    }

    uint64_t string_addrs[PROCESS_ARG_MAX];
    uint64_t rsp = user_stack_top;
    uint32_t argc = launch->argc;

    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        const char* arg = launch->argv[i] != 0 ? launch->argv[i] : "";
        uint32_t len = (uint32_t)strlen64(arg) + 1;
        rsp -= len;
        for (uint32_t j = 0; j < len; j++) {
            *((volatile uint8_t*)(uintptr_t)(rsp + j)) = (uint8_t)arg[j];
        }
        string_addrs[i] = rsp;
    }

    rsp &= ~0x7ULL;
    rsp -= 8;
    *((volatile uint64_t*)(uintptr_t)rsp) = 0;

    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        rsp -= 8;
        *((volatile uint64_t*)(uintptr_t)rsp) = string_addrs[i];
    }

    rsp -= 8;
    *((volatile uint64_t*)(uintptr_t)rsp) = argc;
    rsp &= ~0xFULL;
    process->argc = argc;
    return rsp;
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

static int elf64_has_magic(const uint8_t* image, uint32_t size) {
    if (image == 0 || size < ELF64_EI_NIDENT) {
        return 0;
    }

    return image[ELF64_EI_MAG0] == ELF64_ELFMAG0 &&
           image[ELF64_EI_MAG1] == ELF64_ELFMAG1 &&
           image[ELF64_EI_MAG2] == ELF64_ELFMAG2 &&
           image[ELF64_EI_MAG3] == ELF64_ELFMAG3;
}

static int elf64_validate_supported_image(const uint8_t* image, uint32_t size, const Elf64_Ehdr** out_header) {
    if (!elf64_has_magic(image, size) || size < sizeof(Elf64_Ehdr)) {
        return 0;
    }

    const Elf64_Ehdr* header = (const Elf64_Ehdr*)(const void*)image;
    if (header->e_ident[ELF64_EI_CLASS] != ELF64_CLASS_64) {
        return 0;
    }
    if (header->e_ident[ELF64_EI_DATA] != ELF64_DATA_LSB) {
        return 0;
    }
    if (header->e_ident[ELF64_EI_VERSION] != ELF64_VERSION_CURRENT) {
        return 0;
    }
    if (header->e_ident[ELF64_EI_OSABI] != ELF64_OSABI_SYSTEM_V) {
        return 0;
    }
    if (header->e_type != ELF64_TYPE_EXEC) {
        return 0;
    }
    if (header->e_machine != ELF64_MACHINE_X86_64) {
        return 0;
    }
    if (header->e_version != ELF64_VERSION_CURRENT) {
        return 0;
    }
    if (header->e_ehsize != sizeof(Elf64_Ehdr)) {
        return 0;
    }
    if (header->e_phnum == 0) {
        return 0;
    }
    if (header->e_phentsize != sizeof(Elf64_Phdr)) {
        return 0;
    }
    if (header->e_phoff > size) {
        return 0;
    }
    if ((uint64_t)header->e_phoff + ((uint64_t)header->e_phnum * sizeof(Elf64_Phdr)) > size) {
        return 0;
    }

    if (out_header != 0) {
        *out_header = header;
    }
    return 1;
}

static int elf64_collect_load_info(const uint8_t* image,
                                   uint32_t size,
                                   const Elf64_Ehdr* header,
                                   uint32_t* out_load_count,
                                   uint64_t* out_first_vaddr,
                                   uint64_t* out_last_vaddr,
                                   uint32_t* out_error_code) {
    if (image == 0 || header == 0) {
        if (out_error_code != 0) {
            *out_error_code = 1;
        }
        return 0;
    }

    const Elf64_Phdr* phdrs = (const Elf64_Phdr*)(const void*)(image + header->e_phoff);
    uint32_t load_count = 0;
    uint64_t first_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t last_vaddr = 0;
    int entry_covered = 0;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != ELF64_PT_LOAD) {
            continue;
        }

        if (phdr->p_memsz == 0) {
            if (phdr->p_filesz == 0) {
                continue;
            }
            if (out_error_code != 0) {
                *out_error_code = 2;
            }
            return 0;
        }
        if (phdr->p_filesz > phdr->p_memsz) {
            if (out_error_code != 0) {
                *out_error_code = 3;
            }
            return 0;
        }
        if (phdr->p_offset > size) {
            if (out_error_code != 0) {
                *out_error_code = 4;
            }
            return 0;
        }
        if (phdr->p_offset + phdr->p_filesz > size) {
            if (out_error_code != 0) {
                *out_error_code = 5;
            }
            return 0;
        }
        uint64_t seg_start = phdr->p_vaddr;
        uint64_t seg_end = phdr->p_vaddr + phdr->p_memsz;
        if (seg_end < seg_start) {
            if (out_error_code != 0) {
                *out_error_code = 6;
            }
            return 0;
        }

        if (seg_start < first_vaddr) {
            first_vaddr = seg_start;
        }
        if (seg_end > last_vaddr) {
            last_vaddr = seg_end;
        }
        if (header->e_entry >= seg_start && header->e_entry < seg_end) {
            entry_covered = 1;
        }

        load_count++;
    }

    if (load_count == 0 || !entry_covered) {
        if (out_error_code != 0) {
            *out_error_code = load_count == 0 ? 7 : 8;
        }
        return 0;
    }

    if (out_load_count != 0) {
        *out_load_count = load_count;
    }
    if (out_first_vaddr != 0) {
        *out_first_vaddr = first_vaddr;
    }
    if (out_last_vaddr != 0) {
        *out_last_vaddr = last_vaddr;
    }
    if (out_error_code != 0) {
        *out_error_code = 0;
    }
    return 1;
}

static uint64_t elf64_segment_page_flags(uint32_t segment_flags) {
    uint64_t flags = PAGING64_FLAG_USER;
    if (segment_flags & ELF64_PF_W) {
        flags |= PAGING64_FLAG_WRITABLE;
    }
    return flags;
}

static void copy_process_name(char* dest, const char* src) {
    if (dest == 0) {
        return;
    }

    if (src == 0) {
        dest[0] = '\0';
        return;
    }

    uint32_t i = 0;
    for (; i < PROCESS_NAME_MAX - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static uint32_t infer_shell_prompt_kind(const char* filename) {
    if (filename == 0) {
        return SHELL_PROMPT_NONE;
    }

    if (strcmp64(filename, "USHELL_C.ELF") == 0) {
        return SHELL_PROMPT_CSH;
    }

    if (strcmp64(filename, "USHELL.ELF") == 0 || strcmp64(filename, "USHELL.BIN") == 0) {
        return SHELL_PROMPT_USH;
    }

    return SHELL_PROMPT_NONE;
}

static Process* current_process() {
    if (user_program_depth == 0) {
        return 0;
    }
    return process_stack[user_program_depth - 1];
}

static Process* find_next_ready_process(uint32_t exclude_pid);
static Process* find_process_by_pid(uint32_t pid);
static int resume_user_program(uint32_t pid);

static const char* process_state_name(uint32_t state) {
    if (state == PROCESS_STATE_LOADED) {
        return "loaded";
    }
    if (state == PROCESS_STATE_RUNNING) {
        return "running";
    }
    if (state == PROCESS_STATE_RETURNED) {
        return "returned";
    }
    if (state == PROCESS_STATE_FAILED) {
        return "failed";
    }
    if (state == PROCESS_STATE_PAUSED) {
        return "paused";
    }
    return "empty";
}

static const char* process_term_name(uint32_t reason) {
    if (reason == PROCESS_TERM_EXIT) {
        return "exit";
    }
    if (reason == PROCESS_TERM_LOAD_ERROR) {
        return "load_error";
    }
    if (reason == PROCESS_TERM_READ_ERROR) {
        return "read_error";
    }
    if (reason == PROCESS_TERM_MEMORY_ERROR) {
        return "memory_error";
    }
    if (reason == PROCESS_TERM_MAP_ERROR) {
        return "map_error";
    }
    if (reason == PROCESS_TERM_PAGE_FAULT) {
        return "page_fault";
    }
    if (reason == PROCESS_TERM_GP_FAULT) {
        return "gp_fault";
    }
    if (reason == PROCESS_TERM_DOUBLE_FAULT) {
        return "double_fault";
    }
    if (reason == PROCESS_TERM_KILLED) {
        return "killed";
    }
    return "none";
}

static const char* scheduler_state_name(uint32_t state) {
    if (state == SCHED_STATE_READY) {
        return "ready";
    }
    if (state == SCHED_STATE_RUNNING) {
        return "running";
    }
    if (state == SCHED_STATE_WAITING) {
        return "waiting";
    }
    if (state == SCHED_STATE_FINISHED) {
        return "finished";
    }
    return "none";
}

static const char* pause_reason_name(uint32_t reason) {
    if (reason == PROCESS_PAUSE_YIELD) {
        return "yield";
    }
    if (reason == PROCESS_PAUSE_PREEMPT) {
        return "preempt";
    }
    if (reason == PROCESS_PAUSE_SLEEP) {
        return "sleep";
    }
    return "none";
}

static void process_clear(Process* process) {
    if (process == 0) {
        return;
    }

    process->pid = 0;
    process->parent_pid = 0;
    process->name[0] = '\0';
    process->code_base = 0;
    process->stack_base = 0;
    process->entry_point = 0;
    process->image_size = 0;
    process->code_page_count = 0;
    process->state = PROCESS_STATE_EMPTY;
    process->termination_reason = PROCESS_TERM_NONE;
    process->status_code = 0;
    process->scheduler_state = SCHED_STATE_NONE;
    process->runtime_ticks = 0;
    process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    process->slot_index = 0;
    process->shell_prompt_kind = SHELL_PROMPT_NONE;
    process->argc = 0;
    process->active = 0;
    process->reaped = 0;
    process->resumable = 0;
    process->background = 0;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->wake_tick = 0;
    process->command_line[0] = '\0';
    process->saved_rax = 0;
    process->saved_rbx = 0;
    process->saved_rcx = 0;
    process->saved_rdx = 0;
    process->saved_rbp = 0;
    process->saved_rsi = 0;
    process->saved_rdi = 0;
    process->saved_r8 = 0;
    process->saved_r9 = 0;
    process->saved_r10 = 0;
    process->saved_r11 = 0;
    process->saved_r12 = 0;
    process->saved_r13 = 0;
    process->saved_r14 = 0;
    process->saved_r15 = 0;
    process->saved_rip = 0;
    process->saved_rsp = 0;
    process->saved_rflags = 0;
}

static void process_mark_failed(Process* process, uint32_t reason, uint32_t status_code) {
    if (process == 0) {
        return;
    }

    process->state = PROCESS_STATE_FAILED;
    process->termination_reason = reason;
    process->status_code = status_code;
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->wake_tick = 0;
    process->resumable = 0;
    process->active = 0;
    process->reaped = 0;
}

static void process_mark_returned(Process* process, uint32_t reason, uint32_t status_code) {
    if (process == 0) {
        return;
    }

    process->state = PROCESS_STATE_RETURNED;
    process->termination_reason = reason;
    process->status_code = status_code;
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->wake_tick = 0;
    process->resumable = 0;
    process->active = 0;
    process->reaped = 0;
}

static int scheduler_queue_contains(const Process* process) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        if (sched_queue[index] == process) {
            return 1;
        }
    }
    return 0;
}

static void scheduler_enqueue(Process* process) {
    if (process == 0 || scheduler_queue_contains(process) || sched_queue_count >= SCHED_QUEUE_SIZE) {
        return;
    }

    uint32_t index = (sched_queue_head + sched_queue_count) % SCHED_QUEUE_SIZE;
    sched_queue[index] = process;
    sched_queue_count++;
    process->scheduler_state = SCHED_STATE_READY;
    process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
}

static void scheduler_remove(Process* process) {
    if (process == 0 || sched_queue_count == 0) {
        return;
    }

    Process* compacted[SCHED_QUEUE_SIZE];
    uint32_t kept = 0;
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        if (sched_queue[index] != process) {
            compacted[kept++] = sched_queue[index];
        }
    }

    for (uint32_t i = 0; i < kept; i++) {
        sched_queue[i] = compacted[i];
    }
    for (uint32_t i = kept; i < SCHED_QUEUE_SIZE; i++) {
        sched_queue[i] = 0;
    }
    sched_queue_head = 0;
    sched_queue_count = kept;
}

static void scheduler_mark_running(Process* process) {
    if (process == 0) {
        return;
    }

    process->scheduler_state = SCHED_STATE_RUNNING;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->wake_tick = 0;
    process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    sched_last_pid = process->pid;
    sched_switch_count++;
}

static void scheduler_mark_waiting(Process* process) {
    if (process == 0) {
        return;
    }

    process->scheduler_state = SCHED_STATE_WAITING;
}

static void scheduler_mark_sleeping(Process* process, uint32_t wake_tick) {
    if (process == 0) {
        return;
    }

    scheduler_remove(process);
    process->scheduler_state = SCHED_STATE_WAITING;
    process->wake_tick = wake_tick;
}

static void scheduler_mark_finished(Process* process) {
    if (process == 0) {
        return;
    }

    scheduler_remove(process);
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->timeslice_ticks = 0;
}

static void scheduler_yield_current() {
    Process* process = current_process();
    if (process == 0) {
        return;
    }

    sched_yield_count++;
    process->scheduler_state = SCHED_STATE_READY;
    process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    scheduler_remove(process);
    scheduler_enqueue(process);
}

static void scheduler_on_tick() {
    Process* process = current_process();
    if (process == 0) {
        return;
    }

    process->runtime_ticks++;
    if (process->timeslice_ticks > 0) {
        process->timeslice_ticks--;
    }
}

static void scheduler_wake_sleeping_processes(uint32_t tick_now) {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || !process->active) {
            continue;
        }
        if (process->pause_reason != PROCESS_PAUSE_SLEEP) {
            continue;
        }
        if (process->scheduler_state != SCHED_STATE_WAITING) {
            continue;
        }
        if (tick_now < process->wake_tick) {
            continue;
        }

        process->wake_tick = 0;
        process->scheduler_state = SCHED_STATE_READY;
        process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
        if (!scheduler_queue_contains(process)) {
            scheduler_enqueue(process);
        }
    }
}

static int scheduler_should_preempt_current() {
    Process* process = current_process();
    if (process == 0) {
        return 0;
    }
    if (process->parent_pid == 0) {
        return 0;
    }
    if (process->scheduler_state != SCHED_STATE_RUNNING) {
        return 0;
    }
    if (process->timeslice_ticks != 0) {
        return 0;
    }
    return find_next_ready_process(process->pid) != 0;
}

extern "C" void process_record_fault64(uint32_t reason, uint32_t status_code) {
    process_mark_failed(current_process(), reason, status_code);
}

extern "C" uint64_t process_fault_returnable64() {
    return current_process() != 0 ? 1 : 0;
}

extern "C" void save_yield_context64(uint64_t* frame) {
    Process* process = current_process();
    if (process == 0 || frame == 0) {
        return;
    }

    process->saved_r15 = frame[0];
    process->saved_r14 = frame[1];
    process->saved_r13 = frame[2];
    process->saved_r12 = frame[3];
    process->saved_r11 = frame[4];
    process->saved_r10 = frame[5];
    process->saved_r9  = frame[6];
    process->saved_r8  = frame[7];
    process->saved_rdi = frame[8];
    process->saved_rsi = frame[9];
    process->saved_rbp = frame[10];
    process->saved_rdx = frame[11];
    process->saved_rcx = frame[12];
    process->saved_rbx = frame[13];
    process->saved_rax = 0;
    process->saved_rip = frame[15];
    process->saved_rflags = frame[17];
    process->saved_rsp = frame[18];
    process->state = PROCESS_STATE_PAUSED;
    process->resumable = 1;
    process->pause_reason = PROCESS_PAUSE_YIELD;
    scheduler_yield_current();
}

extern "C" void save_preempt_context64(uint64_t* frame) {
    Process* process = current_process();
    if (process == 0 || frame == 0) {
        return;
    }

    process->saved_r15 = frame[0];
    process->saved_r14 = frame[1];
    process->saved_r13 = frame[2];
    process->saved_r12 = frame[3];
    process->saved_r11 = frame[4];
    process->saved_r10 = frame[5];
    process->saved_r9  = frame[6];
    process->saved_r8  = frame[7];
    process->saved_rdi = frame[8];
    process->saved_rsi = frame[9];
    process->saved_rbp = frame[10];
    process->saved_rdx = frame[11];
    process->saved_rcx = frame[12];
    process->saved_rbx = frame[13];
    process->saved_rax = 0;
    process->saved_rip = frame[15];
    process->saved_rflags = frame[17];
    process->saved_rsp = frame[18];
    process->state = PROCESS_STATE_PAUSED;
    process->resumable = 1;
    process->pause_reason = PROCESS_PAUSE_PREEMPT;
    scheduler_yield_current();
}

extern "C" void save_sleep_context64(uint64_t* frame, uint32_t sleep_ticks) {
    Process* process = current_process();
    if (process == 0 || frame == 0) {
        return;
    }

    process->saved_r15 = frame[0];
    process->saved_r14 = frame[1];
    process->saved_r13 = frame[2];
    process->saved_r12 = frame[3];
    process->saved_r11 = frame[4];
    process->saved_r10 = frame[5];
    process->saved_r9  = frame[6];
    process->saved_r8  = frame[7];
    process->saved_rdi = frame[8];
    process->saved_rsi = frame[9];
    process->saved_rbp = frame[10];
    process->saved_rdx = frame[11];
    process->saved_rcx = frame[12];
    process->saved_rbx = frame[13];
    process->saved_rax = 0;
    process->saved_rip = frame[15];
    process->saved_rflags = frame[17];
    process->saved_rsp = frame[18];
    process->state = PROCESS_STATE_PAUSED;
    process->resumable = 1;
    process->pause_reason = PROCESS_PAUSE_SLEEP;
    scheduler_mark_sleeping(process, pit.get_tick() + sleep_ticks);
}

static const char* pause_action_name(const Process* process) {
    if (process != 0 && process->pause_reason == PROCESS_PAUSE_PREEMPT) {
        return "Preempted";
    }
    if (process != 0 && process->pause_reason == PROCESS_PAUSE_SLEEP) {
        return "Sleeping";
    }
    return "Yielded";
}

static void print_process_summary(const Process* process) {
    if (process == 0 || process->pid == 0) {
        print("none");
        return;
    }

    print("pid=");
    print_hex32(process->pid);
    print(" name=");
    print(process->name);
    print(" parent=");
    print_hex32(process->parent_pid);
    print(" slot=");
    print_hex32(process->slot_index);
    print(" state=");
    print(process_state_name(process->state));
    print(" term=");
    print(process_term_name(process->termination_reason));
    print(" code=");
    print_hex32(process->status_code);
    print(" sched=");
    print(scheduler_state_name(process->scheduler_state));
    print(" pause=");
    print(pause_reason_name(process->pause_reason));
    print(" mode=");
    print(process->background ? "bg" : "fg");
    print(" ticks=");
    print_hex32(process->runtime_ticks);
    print(" slice=");
    print_hex32(process->timeslice_ticks);
    if (process->scheduler_state == SCHED_STATE_WAITING && process->pause_reason == PROCESS_PAUSE_SLEEP) {
        uint32_t tick_now = pit.get_tick();
        uint32_t remaining = process->wake_tick > tick_now ? (process->wake_tick - tick_now) : 0;
        print(" wake=");
        print_hex32(process->wake_tick);
        print(" remain=");
        print_hex32(remaining);
    }
    print(" reaped=");
    print(process->reaped ? "yes" : "no");
}

static void print_process_table() {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        print("\n[");
        print_hex32(i);
        print("] ");
        print_process_summary(&process_table[i]);
    }
    print("\n");
}

static void print_job_compact(const char* label, const Process* process) {
    if (process == 0 || process->pid == 0) {
        print("\n");
        print(label);
        print(": none");
        return;
    }

    print("\n");
    print(label);
    print(": pid=");
    print_hex32(process->pid);
    print(" ");
    print(process_state_name(process->state));
    print(" ");
    print(process->background ? "bg" : "fg");

    if (process->scheduler_state != SCHED_STATE_RUNNING) {
        print(" ");
        print(scheduler_state_name(process->scheduler_state));
    }

    if (process->pause_reason != PROCESS_PAUSE_NONE) {
        print("/");
        print(pause_reason_name(process->pause_reason));
    }

    if (process->termination_reason != PROCESS_TERM_NONE) {
        print(" ");
        print(process_term_name(process->termination_reason));
        if (process->termination_reason == PROCESS_TERM_EXIT ||
            process->termination_reason == PROCESS_TERM_PAGE_FAULT ||
            process->termination_reason == PROCESS_TERM_LOAD_ERROR ||
            process->termination_reason == PROCESS_TERM_KILLED) {
            print(" code=");
            print_hex32(process->status_code);
        }
    }

    if (process->scheduler_state == SCHED_STATE_WAITING &&
        process->pause_reason == PROCESS_PAUSE_SLEEP) {
        uint32_t tick_now = pit.get_tick();
        uint32_t remaining = process->wake_tick > tick_now ? (process->wake_tick - tick_now) : 0;
        print(" remain=");
        print_hex32(remaining);
    }

    print(" ");
    print(process->name);
}

static void print_child_result_compact(const char* label, const Process* process) {
    if (process == 0 || process->pid == 0) {
        print("\n");
        print(label);
        print(": none");
        return;
    }

    print("\n");
    print(label);
    print(": pid=");
    print_hex32(process->pid);
    print(" ");
    print(process_state_name(process->state));

    if (process->termination_reason != PROCESS_TERM_NONE) {
        print(" ");
        print(process_term_name(process->termination_reason));
    }

    print(" code=");
    print_hex32(process->status_code);

    if (process->reaped) {
        print(" reaped");
    }

    print(" ");
    print(process->name);
}

static void print_jobs_for_process(const Process* parent) {
    print("\n=== JOBS ===");
    if (parent == 0) {
        print("\nNo current user process.");
        print("\n============");
        return;
    }

    print_job_compact("self", parent);

    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent->pid) {
            continue;
        }
        char label[16];
        label[0] = 'j';
        label[1] = 'o';
        label[2] = 'b';
        label[3] = '[';
        label[4] = (char)('0' + (count % 10));
        label[5] = ']';
        label[6] = '\0';
        print_job_compact(label, process);
        count++;
    }

    if (count == 0) {
        print("\n(no child jobs)");
    }
    print("\n(use ps for full details)");
    print("\n============\n");
}

static void print_scheduler_info() {
    print("\n=== SCHEDULER ===");
    print("\nQueue count: ");
    print_hex32(sched_queue_count);
    print("\nHead: ");
    print_hex32(sched_queue_head);
    print("\nLast PID: ");
    print_hex32(sched_last_pid);
    print("\nSwitches: ");
    print_hex32(sched_switch_count);
    print("\nYields: ");
    print_hex32(sched_yield_count);

    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        print("\nQ[");
        print_hex32(i);
        print("] ");
        print_process_summary(sched_queue[index]);
    }
    print("\n=================\n");
}

static void print_vfs_mounts() {
    print("\n=== VFS MOUNTS ===");

    uint32_t count = vfs_mount_count();
    if (count == 0) {
        print("\n(no mounts)");
        print("\n==================\n");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        VFSMountInfo info;
        if (vfs_get_mount_info(i, &info) != VFS_OK) {
            continue;
        }

        print("\nmount[");
        print_hex32(i);
        print("] ");
        print(info.mount_path);
        print(" fs=");
        print(info.fs_name);
        print(" backend=");
        print_hex32(info.backend_kind);
    }

    print("\n==================\n");
}

static const char* current_process_shell_prompt() {
    Process* process = current_process();
    if (process == 0) {
        return 0;
    }

    if (process->shell_prompt_kind == SHELL_PROMPT_CSH) {
        return "csh> ";
    }

    if (process->shell_prompt_kind == SHELL_PROMPT_USH) {
        return "ush> ";
    }

    return 0;
}

static void redraw_user_shell_prompt_if_needed() {
    const char* prompt = current_process_shell_prompt();
    if (prompt == 0) {
        return;
    }

    print(prompt);
}

static int process_record_is_active(const Process* process) {
    if (process == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < USER_PROGRAM_SLOT_COUNT; i++) {
        if (process_stack[i] == process) {
            return 1;
        }
    }
    return 0;
}

static int execution_slot_in_use(uint32_t slot_index) {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || !process->active) {
            continue;
        }
        if (process->slot_index == slot_index) {
            return 1;
        }
    }
    return 0;
}

static int allocate_execution_slot(uint32_t* slot_index_out) {
    if (slot_index_out == 0) {
        return 0;
    }

    for (uint32_t slot = 0; slot < USER_PROGRAM_SLOT_COUNT; slot++) {
        if (!execution_slot_in_use(slot)) {
            *slot_index_out = slot;
            return 1;
        }
    }

    return 0;
}

static void get_execution_slot_bases(uint32_t slot_index, uint64_t* code_base, uint64_t* stack_base) {
    uint64_t code = USER_SLOT0_CODE_BASE;
    uint64_t stack = USER_SLOT0_STACK_BASE;

    if (slot_index == 1) {
        code = USER_SLOT1_CODE_BASE;
        stack = USER_SLOT1_STACK_BASE;
    } else if (slot_index == 2) {
        code = USER_SLOT2_CODE_BASE;
        stack = USER_SLOT2_STACK_BASE;
    } else if (slot_index == 3) {
        code = USER_SLOT3_CODE_BASE;
        stack = USER_SLOT3_STACK_BASE;
    }

    if (code_base != 0) {
        *code_base = code;
    }
    if (stack_base != 0) {
        *stack_base = stack;
    }
}

static Process* allocate_process_record() {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_table[i].pid == 0) {
            process_clear(&process_table[i]);
            return &process_table[i];
        }
    }

    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (!process_record_is_active(&process_table[i]) &&
            !process_table[i].active &&
            process_table[i].reaped) {
            process_clear(&process_table[i]);
            return &process_table[i];
        }
    }

    return 0;
}

static const Process* find_last_child_process(uint32_t parent_pid) {
    const Process* latest = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent_pid) {
            continue;
        }
        if (latest == 0 || process->pid > latest->pid) {
            latest = process;
        }
    }
    return latest;
}

static Process* find_waitable_child_process(uint32_t parent_pid) {
    Process* latest = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent_pid) {
            continue;
        }
        if (process->active || process->reaped) {
            continue;
        }
        if (process->state != PROCESS_STATE_RETURNED && process->state != PROCESS_STATE_FAILED) {
            continue;
        }
        if (latest == 0 || process->pid > latest->pid) {
            latest = process;
        }
    }
    return latest;
}

static uint32_t reap_all_child_processes(uint32_t parent_pid) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent_pid) {
            continue;
        }
        if (process->active || process->reaped) {
            continue;
        }
        if (process->state != PROCESS_STATE_RETURNED && process->state != PROCESS_STATE_FAILED) {
            continue;
        }
        process->reaped = 1;
        count++;
    }
    return count;
}

static uint32_t count_unfinished_child_processes(uint32_t parent_pid) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent_pid) {
            continue;
        }
        if (process->state == PROCESS_STATE_RETURNED || process->state == PROCESS_STATE_FAILED) {
            continue;
        }
        count++;
    }
    return count;
}

static Process* find_last_paused_child_process(uint32_t parent_pid) {
    Process* latest = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent_pid) {
            continue;
        }
        if (process->state != PROCESS_STATE_PAUSED || !process->resumable) {
            continue;
        }
        if (latest == 0 || process->pid > latest->pid) {
            latest = process;
        }
    }
    return latest;
}

static Process* find_next_ready_process(uint32_t exclude_pid) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Process* process = sched_queue[index];
        if (process == 0) {
            continue;
        }
        if (process->pid == 0 || process->pid == exclude_pid) {
            continue;
        }
        if (process->state != PROCESS_STATE_PAUSED || !process->resumable) {
            continue;
        }
        if (process->scheduler_state != SCHED_STATE_READY) {
            continue;
        }
        return process;
    }
    return 0;
}

static Process* find_next_background_ready_process(uint32_t exclude_pid) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Process* process = sched_queue[index];
        if (process == 0) {
            continue;
        }
        if (process->pid == 0 || process->pid == exclude_pid) {
            continue;
        }
        if (process->state != PROCESS_STATE_PAUSED || !process->resumable) {
            continue;
        }
        if (process->scheduler_state != SCHED_STATE_READY) {
            continue;
        }
        if (!process->background) {
            continue;
        }
        return process;
    }
    return 0;
}

static Process* find_next_woken_process(uint32_t exclude_pid) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Process* process = sched_queue[index];
        if (process == 0) {
            continue;
        }
        if (process->pid == 0 || process->pid == exclude_pid) {
            continue;
        }
        if (process->state != PROCESS_STATE_PAUSED || !process->resumable) {
            continue;
        }
        if (process->scheduler_state != SCHED_STATE_READY) {
            continue;
        }
        if (process->pause_reason != PROCESS_PAUSE_SLEEP) {
            continue;
        }
        return process;
    }
    return 0;
}

static int resume_user_program_internal(Process* parent, Process* process, int print_banner);
static int idle_until_ready_process();

static int parent_should_resume_immediately(const Process* parent) {
    if (parent == 0 || !parent->active) {
        return 0;
    }
    if (parent->pause_reason == PROCESS_PAUSE_SLEEP &&
        parent->scheduler_state == SCHED_STATE_WAITING) {
        return 0;
    }
    return 1;
}

static int continue_ready_processes(uint32_t exclude_pid) {
    Process* next_ready = find_next_ready_process(exclude_pid);
    if (next_ready == 0) {
        return 0;
    }

    Process* parent = next_ready->parent_pid != 0 ? find_process_by_pid(next_ready->parent_pid) : 0;

    print("Auto-switching to ready process [pid=");
    print_hex32(next_ready->pid);
    print("].\n");
    return resume_user_program_internal(parent, next_ready, 1);
}

static int continue_woken_processes(uint32_t exclude_pid) {
    Process* next_ready = find_next_woken_process(exclude_pid);
    if (next_ready == 0) {
        return 0;
    }

    Process* parent = next_ready->parent_pid != 0 ? find_process_by_pid(next_ready->parent_pid) : 0;

    print("Auto-switching to ready process [pid=");
    print_hex32(next_ready->pid);
    print("].\n");
    return resume_user_program_internal(parent, next_ready, 1);
}

static int continue_background_processes(uint32_t exclude_pid) {
    Process* next_ready = find_next_background_ready_process(exclude_pid);
    if (next_ready == 0) {
        return 0;
    }

    Process* parent = next_ready->parent_pid != 0 ? find_process_by_pid(next_ready->parent_pid) : 0;

    print("Auto-switching to background process [pid=");
    print_hex32(next_ready->pid);
    print("].\n");
    return resume_user_program_internal(parent, next_ready, 1);
}

static int idle_until_ready_process() {
    while (1) {
        if (continue_ready_processes(0)) {
            return 1;
        }
        __asm__ volatile("sti; hlt; cli");
    }
}

static Process* find_process_by_pid(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return 0;
}

static void cleanup_user_process_mapping(Process* process) {
    if (process == 0) {
        return;
    }

    for (uint32_t page = 0; page < process->code_page_count; page++) {
        uint64_t virt = process->code_base + ((uint64_t)page * PAGING64_PAGE_SIZE);
        uint64_t code_phys = paging64_get_phys(virt);
        if (code_phys != 0) {
            paging64_unmap_page(virt);
            pmm64_free_block((void*)(uintptr_t)code_phys);
        }
    }

    uint64_t stack_phys = paging64_get_phys(process->stack_base);
    if (stack_phys != 0) {
        paging64_unmap_page(process->stack_base);
        pmm64_free_block((void*)(uintptr_t)stack_phys);
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

static int copy_user_buffer(const uint8_t* user_ptr, uint8_t* kernel_buf, uint32_t size) {
    if ((size > 0 && user_ptr == 0) || (size > 0 && kernel_buf == 0)) {
        return 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + i)) == 0) {
            return 0;
        }
        kernel_buf[i] = user_ptr[i];
    }
    return 1;
}

static int copy_kernel_to_user_buffer(uint8_t* user_ptr, const uint8_t* kernel_buf, uint32_t size) {
    if ((size > 0 && user_ptr == 0) || (size > 0 && kernel_buf == 0)) {
        return 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (paging64_get_phys((uint64_t)(uintptr_t)(user_ptr + i)) == 0) {
            return 0;
        }
        user_ptr[i] = kernel_buf[i];
    }
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
    print("\nNext PID: ");
    print_hex32(next_pid);
    print("\nCurrent process: ");
    print_process_summary(current_process());
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
        print_process_summary(&process_table[i]);
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
    print("\nfree, dump, sched, mounts, atatest, ls [path], load, save, rm, pagefault, uptime");
    print("\nrun, resume, usertest, ushell, ushellc");
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

static void command_sched() {
    print_scheduler_info();
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

static int run_user_program(const char* command_line) {
    if (command_line == 0 || command_line[0] == '\0') {
        print("\nUser program filename is empty.");
        return 0;
    }

    if (user_program_depth >= USER_PROGRAM_SLOT_COUNT) {
        print("\nUser program nesting limit reached.");
        return 0;
    }

    uint32_t slot_index = 0;
    if (!allocate_execution_slot(&slot_index)) {
        print("\nNo free execution slot. Resume or finish paused programs first.");
        return 0;
    }
    uint32_t stack_index = user_program_depth;
    uint64_t user_code_base = 0;
    uint64_t user_stack_base = 0;
    get_execution_slot_bases(slot_index, &user_code_base, &user_stack_base);
    uint64_t user_stack_top = user_stack_base + PAGING64_PAGE_SIZE;
    Process* parent = current_process();
    Process* process = allocate_process_record();
    if (process == 0) {
        print("\nProcess table is full. Reap finished child results with wait.");
        return 0;
    }
    process->pid = next_pid++;
    process->parent_pid = parent != 0 ? parent->pid : 0;
    process->slot_index = slot_index;
    copy_string64(process->command_line, sizeof(process->command_line), command_line);
    UserLaunchInfo launch;
    if (parse_launch_command(process->command_line, &launch) == 0 || launch.argv[0] == 0) {
        process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 7);
        scheduler_mark_finished(process);
        print("\nUser program filename is empty.");
        return 0;
    }
    const char* filename = launch.argv[0];
    copy_process_name(process->name, filename);
    process->shell_prompt_kind = infer_shell_prompt_kind(filename);
    process->code_base = user_code_base;
    process->stack_base = user_stack_base;
    process->entry_point = user_code_base;
    process->state = PROCESS_STATE_LOADED;
    process->termination_reason = PROCESS_TERM_NONE;
    process->status_code = 0;
    process->active = 1;
    scheduler_enqueue(process);

    VFSFileInfo file_info;
    if (vfs_get_file_info(filename, &file_info) != VFS_OK) {
        process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 1);
        scheduler_mark_finished(process);
        print("\nUser program not found: ");
        print(filename);
        print("\n");
        return 0;
    }

    uint32_t max_user_image_size = (uint32_t)(user_stack_base - user_code_base);
    if (file_info.size == 0 || file_info.size > max_user_image_size) {
        process->image_size = file_info.size;
        process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 2);
        scheduler_mark_finished(process);
        print("\nUser program size is invalid for the current loader.\n");
        return 0;
    }
    process->image_size = file_info.size;

    uint32_t program_buffer_size = file_info.size;
    if (program_buffer_size < 512) {
        program_buffer_size = 512;
    }

    uint8_t* program_buffer = (uint8_t*)kmalloc(program_buffer_size);
    if (program_buffer == 0) {
        process_mark_failed(process, PROCESS_TERM_MEMORY_ERROR, 1);
        scheduler_mark_finished(process);
        print("\nOut of memory for user program.");
        return 0;
    }

    uint32_t bytes_read = 0;
    if (vfs_read_file(filename, program_buffer, program_buffer_size, &bytes_read) != VFS_OK) {
        process_mark_failed(process, PROCESS_TERM_READ_ERROR, 1);
        scheduler_mark_finished(process);
        print("\nFailed to read user program: ");
        print(filename);
        print("\n");
        kfree(program_buffer);
        return 0;
    }

    const Elf64_Ehdr* elf_header = 0;
    uint32_t code_page_count = 1;
    int is_elf_image = 0;
    uint64_t elf_first_vaddr = 0;
    uint64_t elf_last_vaddr = 0;
    uint32_t elf_load_error = 0;
    if (elf64_has_magic(program_buffer, file_info.size)) {
        if (!elf64_validate_supported_image(program_buffer, file_info.size, &elf_header)) {
            process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 3);
            scheduler_mark_finished(process);
            print("\nInvalid or unsupported ELF64 user program: ");
            print(filename);
            print("\n");
            kfree(program_buffer);
            return 0;
        }

        uint32_t elf_load_count = 0;
        if (!elf64_collect_load_info(program_buffer,
                                     file_info.size,
                                     elf_header,
                                     &elf_load_count,
                                     &elf_first_vaddr,
                                     &elf_last_vaddr,
                                     &elf_load_error)) {
            process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 4);
            scheduler_mark_finished(process);
            print("\nInvalid ELF64 loadable segments: ");
            print(filename);
            print(" [reason=");
            print_hex32(elf_load_error);
            print("]");
            print("\n");
            kfree(program_buffer);
            return 0;
        }

        uint64_t elf_load_size = elf_last_vaddr - elf_first_vaddr;
        if (elf_load_size == 0 || elf_load_size > (user_stack_base - user_code_base)) {
            process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 5);
            scheduler_mark_finished(process);
            print("\nELF64 load range is too large: ");
            print(filename);
            print("\n");
            kfree(program_buffer);
            return 0;
        }

        code_page_count = (uint32_t)((elf_load_size + PAGING64_PAGE_SIZE - 1) / PAGING64_PAGE_SIZE);
        process->entry_point = user_code_base + (elf_header->e_entry - elf_first_vaddr);
        is_elf_image = 1;
    } else {
        if (file_info.size > PAGING64_PAGE_SIZE) {
            process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 6);
            scheduler_mark_finished(process);
            print("\nFlat user program is too large for the current loader.\n");
            kfree(program_buffer);
            return 0;
        }

        process->entry_point = user_code_base;
    }

    uint64_t* elf_page_flags = 0;
    if (is_elf_image) {
        elf_page_flags = (uint64_t*)kmalloc(sizeof(uint64_t) * code_page_count);
        if (elf_page_flags == 0) {
            kfree(program_buffer);
            process_mark_failed(process, PROCESS_TERM_MEMORY_ERROR, 3);
            scheduler_mark_finished(process);
            print("\nOut of memory for ELF page permissions.");
            return 0;
        }
        for (uint32_t i = 0; i < code_page_count; i++) {
            elf_page_flags[i] = PAGING64_FLAG_USER;
        }

        const Elf64_Phdr* phdrs = (const Elf64_Phdr*)(const void*)(program_buffer + elf_header->e_phoff);
        for (uint16_t i = 0; i < elf_header->e_phnum; i++) {
            const Elf64_Phdr* phdr = &phdrs[i];
            if (phdr->p_type != ELF64_PT_LOAD) {
                continue;
            }

            uint64_t seg_start = phdr->p_vaddr - elf_first_vaddr;
            uint64_t seg_end = seg_start + phdr->p_memsz;
            uint32_t first_page = (uint32_t)(seg_start / PAGING64_PAGE_SIZE);
            uint32_t last_page = (uint32_t)((seg_end - 1) / PAGING64_PAGE_SIZE);
            uint64_t final_flags = elf64_segment_page_flags(phdr->p_flags);
            for (uint32_t page = first_page; page <= last_page; page++) {
                elf_page_flags[page] |= (final_flags & PAGING64_FLAG_WRITABLE);
            }
        }
    }

    uint32_t mapped_code_pages = 0;
    for (uint32_t page = 0; page < code_page_count; page++) {
        uint64_t code_phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
        if (code_phys == 0) {
            process->code_page_count = mapped_code_pages;
            cleanup_user_process_mapping(process);
            kfree(program_buffer);
            if (elf_page_flags != 0) {
                kfree(elf_page_flags);
            }
            process_mark_failed(process, PROCESS_TERM_MEMORY_ERROR, 2);
            scheduler_mark_finished(process);
            print("\nFailed to allocate user program pages.");
            return 0;
        }

        uint64_t virt = user_code_base + ((uint64_t)page * PAGING64_PAGE_SIZE);
        if (!paging64_map_page(virt, code_phys, PAGING64_FLAG_WRITABLE | PAGING64_FLAG_USER)) {
            pmm64_free_block((void*)(uintptr_t)code_phys);
            process->code_page_count = mapped_code_pages;
            cleanup_user_process_mapping(process);
            kfree(program_buffer);
            if (elf_page_flags != 0) {
                kfree(elf_page_flags);
            }
            process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 1);
            scheduler_mark_finished(process);
            print("\nFailed to map user code page.");
            return 0;
        }

        for (uint64_t i = 0; i < PAGING64_PAGE_SIZE; i++) {
            *((volatile uint8_t*)(uintptr_t)(virt + i)) = 0;
        }
        mapped_code_pages++;
    }
    process->code_page_count = code_page_count;

    uint64_t stack_phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
    if (stack_phys == 0) {
        cleanup_user_process_mapping(process);
        process->code_page_count = 0;
        kfree(program_buffer);
        if (elf_page_flags != 0) {
            kfree(elf_page_flags);
        }
        process_mark_failed(process, PROCESS_TERM_MEMORY_ERROR, 2);
        scheduler_mark_finished(process);
        print("\nFailed to allocate user program pages.");
        return 0;
    }

    if (!paging64_map_page(user_stack_base, stack_phys, PAGING64_FLAG_WRITABLE | PAGING64_FLAG_USER)) {
        pmm64_free_block((void*)(uintptr_t)stack_phys);
        cleanup_user_process_mapping(process);
        process->code_page_count = 0;
        kfree(program_buffer);
        if (elf_page_flags != 0) {
            kfree(elf_page_flags);
        }
        process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 2);
        scheduler_mark_finished(process);
        print("\nFailed to map user stack page.");
        return 0;
    }

    for (uint64_t i = 0; i < PAGING64_PAGE_SIZE; i++) {
        *((volatile uint8_t*)(uintptr_t)(user_stack_base + i)) = 0;
    }

    if (is_elf_image) {
        const Elf64_Phdr* phdrs = (const Elf64_Phdr*)(const void*)(program_buffer + elf_header->e_phoff);
        for (uint16_t i = 0; i < elf_header->e_phnum; i++) {
            const Elf64_Phdr* phdr = &phdrs[i];
            if (phdr->p_type != ELF64_PT_LOAD) {
                continue;
            }

            uint64_t dest = user_code_base + (phdr->p_vaddr - elf_first_vaddr);
            for (uint64_t j = 0; j < phdr->p_filesz; j++) {
                *((volatile uint8_t*)(uintptr_t)(dest + j)) = program_buffer[phdr->p_offset + j];
            }
            for (uint64_t j = phdr->p_filesz; j < phdr->p_memsz; j++) {
                *((volatile uint8_t*)(uintptr_t)(dest + j)) = 0;
            }
        }

        for (uint32_t page = 0; page < code_page_count; page++) {
            uint64_t virt = user_code_base + ((uint64_t)page * PAGING64_PAGE_SIZE);
            uint64_t phys = paging64_get_phys(virt);
            if (phys == 0 || !paging64_map_page(virt, phys & 0x000FFFFFFFFFF000ULL, elf_page_flags[page])) {
                cleanup_user_process_mapping(process);
                process->code_page_count = 0;
                kfree(program_buffer);
                kfree(elf_page_flags);
                process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 3);
                scheduler_mark_finished(process);
                print("\nFailed to apply ELF page permissions.");
                return 0;
            }
        }
        kfree(elf_page_flags);
    } else {
        for (uint32_t i = 0; i < file_info.size; i++) {
            *((volatile uint8_t*)(uintptr_t)(user_code_base + i)) = program_buffer[i];
        }
    }
    kfree(program_buffer);

    process->state = PROCESS_STATE_LOADED;

    uint64_t saved_rsp0 = gdt64_get_kernel_stack();
    uint8_t saved_pic1_mask = inb(0x21);
    int saved_user_input_mode = user_input_mode;
    uint64_t saved_return_rsp = kernel_user_return_rsp;
    uint64_t saved_rbx = kernel_user_saved_rbx;
    uint64_t saved_rbp = kernel_user_saved_rbp;
    uint64_t saved_r12 = kernel_user_saved_r12;
    uint64_t saved_r13 = kernel_user_saved_r13;
    uint64_t saved_r14 = kernel_user_saved_r14;
    uint64_t saved_r15 = kernel_user_saved_r15;
    print("\nRunning user program: ");
    print(filename);
    print(" [pid=");
    print_hex32(process->pid);
    print(" parent=");
    print_hex32(process->parent_pid);
    print("]");
    print("\n");
    user_input_reset();
    user_input_mode = 1;
    outb(0x21, saved_pic1_mask | 0x02);
    gdt64_set_kernel_stack(current_rsp() - 8);
    process->state = PROCESS_STATE_RUNNING;
    if (parent != 0) {
        scheduler_mark_waiting(parent);
    }
    scheduler_mark_running(process);
    process_stack[stack_index] = process;
    user_program_depth++;
    uint64_t initial_user_rsp = prepare_user_stack_with_argv(process, user_stack_top, &launch);
    enter_user_mode(process->entry_point, initial_user_rsp);
    user_program_depth--;
    process_stack[stack_index] = 0;

    outb(0x21, saved_pic1_mask);
    user_input_mode = saved_user_input_mode;
    user_input_reset();
    gdt64_set_kernel_stack(saved_rsp0);
    kernel_user_return_rsp = saved_return_rsp;
    kernel_user_saved_rbx = saved_rbx;
    kernel_user_saved_rbp = saved_rbp;
    kernel_user_saved_r12 = saved_r12;
    kernel_user_saved_r13 = saved_r13;
    kernel_user_saved_r14 = saved_r14;
    kernel_user_saved_r15 = saved_r15;

    if (process->state == PROCESS_STATE_PAUSED) {
        print("\n");
        print(pause_action_name(process));
        print(" from user program [pid=");
        print_hex32(process->pid);
        print("].\n");

        if (continue_ready_processes(process->pid)) {
            return 1;
        }

        if (parent_should_resume_immediately(parent)) {
            scheduler_mark_running(parent);
            return 1;
        }
        return idle_until_ready_process();
    }

    cleanup_user_process_mapping(process);
    process->code_page_count = 0;

    if (process->state != PROCESS_STATE_FAILED && process->state != PROCESS_STATE_RETURNED) {
        process_mark_returned(process, PROCESS_TERM_NONE, 0);
    }
    process->resumable = 0;
    scheduler_mark_finished(process);
    if (parent != 0 && parent->active) {
        scheduler_mark_running(parent);
    }
    print("\nReturned from user program [pid=");
    print_hex32(process->pid);
    print("] state=");
    print(process_state_name(process->state));
    print(" term=");
    print(process_term_name(process->termination_reason));
    print(" code=");
    print_hex32(process->status_code);
    print(".\n");
    process->active = 0;

    if (continue_ready_processes(process->pid)) {
        return 1;
    }

    if (parent == 0) {
        return 1;
    }

    if (parent_should_resume_immediately(parent)) {
        scheduler_mark_running(parent);
        return 1;
    }
    return idle_until_ready_process();
}

static int resume_user_program_internal(Process* parent, Process* process, int print_banner) {
    if (process == 0) {
        return 0;
    }
    uint32_t stack_index = user_program_depth;

    uint64_t saved_rsp0 = gdt64_get_kernel_stack();
    uint8_t saved_pic1_mask = inb(0x21);
    int saved_user_input_mode = user_input_mode;
    uint64_t saved_return_rsp = kernel_user_return_rsp;
    uint64_t saved_rbx = kernel_user_saved_rbx;
    uint64_t saved_rbp = kernel_user_saved_rbp;
    uint64_t saved_r12 = kernel_user_saved_r12;
    uint64_t saved_r13 = kernel_user_saved_r13;
    uint64_t saved_r14 = kernel_user_saved_r14;
    uint64_t saved_r15 = kernel_user_saved_r15;

    if (print_banner) {
        print("\nResuming user program [pid=");
        print_hex32(process->pid);
        print("].\n");
    }

    kernel_user_resume_rax = process->saved_rax;
    kernel_user_resume_rbx = process->saved_rbx;
    kernel_user_resume_rcx = process->saved_rcx;
    kernel_user_resume_rdx = process->saved_rdx;
    kernel_user_resume_rbp = process->saved_rbp;
    kernel_user_resume_rsi = process->saved_rsi;
    kernel_user_resume_rdi = process->saved_rdi;
    kernel_user_resume_r8 = process->saved_r8;
    kernel_user_resume_r9 = process->saved_r9;
    kernel_user_resume_r10 = process->saved_r10;
    kernel_user_resume_r11 = process->saved_r11;
    kernel_user_resume_r12 = process->saved_r12;
    kernel_user_resume_r13 = process->saved_r13;
    kernel_user_resume_r14 = process->saved_r14;
    kernel_user_resume_r15 = process->saved_r15;
    kernel_user_resume_rip = process->saved_rip;
    kernel_user_resume_rsp = process->saved_rsp;
    kernel_user_resume_rflags = process->saved_rflags;

    user_input_reset();
    user_input_mode = 1;
    outb(0x21, saved_pic1_mask | 0x02);
    gdt64_set_kernel_stack(current_rsp() - 8);
    scheduler_mark_waiting(parent);
    scheduler_mark_running(process);
    process->state = PROCESS_STATE_RUNNING;
    process->resumable = 0;
    process_stack[stack_index] = process;
    user_program_depth++;
    resume_user_mode();
    user_program_depth--;
    process_stack[stack_index] = 0;

    outb(0x21, saved_pic1_mask);
    user_input_mode = saved_user_input_mode;
    user_input_reset();
    gdt64_set_kernel_stack(saved_rsp0);
    kernel_user_return_rsp = saved_return_rsp;
    kernel_user_saved_rbx = saved_rbx;
    kernel_user_saved_rbp = saved_rbp;
    kernel_user_saved_r12 = saved_r12;
    kernel_user_saved_r13 = saved_r13;
    kernel_user_saved_r14 = saved_r14;
    kernel_user_saved_r15 = saved_r15;

    if (process->state == PROCESS_STATE_PAUSED) {
        print("\n");
        print(pause_action_name(process));
        print(" from user program [pid=");
        print_hex32(process->pid);
        print("].\n");

        if (continue_ready_processes(process->pid)) {
            return 1;
        }

        if (parent_should_resume_immediately(parent)) {
            scheduler_mark_running(parent);
            return 1;
        }
        return idle_until_ready_process();
    }

    cleanup_user_process_mapping(process);
    process->code_page_count = 0;
    if (process->state != PROCESS_STATE_FAILED && process->state != PROCESS_STATE_RETURNED) {
        process_mark_returned(process, PROCESS_TERM_NONE, 0);
    }
    process->resumable = 0;
    scheduler_mark_finished(process);
    if (parent_should_resume_immediately(parent)) {
        scheduler_mark_running(parent);
    }
    print("\nReturned from user program [pid=");
    print_hex32(process->pid);
    print("] state=");
    print(process_state_name(process->state));
    print(" term=");
    print(process_term_name(process->termination_reason));
    print(" code=");
    print_hex32(process->status_code);
    print(".\n");

    if (continue_ready_processes(process->pid)) {
        return 1;
    }

    if (parent == 0) {
        return 1;
    }

    if (parent_should_resume_immediately(parent)) {
        scheduler_mark_running(parent);
        return 1;
    }
    return idle_until_ready_process();
}

static int resume_user_program(uint32_t pid) {
    Process* parent = current_process();
    if (parent == 0) {
        print("\nNo current parent process.");
        return 0;
    }

    Process* process = pid == 0 ? find_last_paused_child_process(parent->pid) : find_process_by_pid(pid);
    if (process == 0) {
        print("\nProcess not found.\n");
        return 0;
    }
    if (process->parent_pid != parent->pid) {
        print("\nProcess is not a child of the current process.\n");
        return 0;
    }
    if (process->scheduler_state == SCHED_STATE_WAITING && process->pause_reason == PROCESS_PAUSE_SLEEP) {
        print("\nProcess is sleeping.\n");
        return 0;
    }
    if (!process->resumable || process->state != PROCESS_STATE_PAUSED) {
        print("\nProcess is not paused.\n");
        return 0;
    }
    if (user_program_depth >= USER_PROGRAM_SLOT_COUNT) {
        print("\nUser program nesting limit reached.");
        return 0;
    }

    return resume_user_program_internal(parent, process, 1);
}

static int kill_user_program(uint32_t pid) {
    Process* parent = current_process();
    if (parent == 0) {
        print("\nNo current parent process.");
        return 0;
    }

    Process* process = find_process_by_pid(pid);
    if (process == 0) {
        print("\nProcess not found.\n");
        return 0;
    }
    if (process->parent_pid != parent->pid) {
        print("\nProcess is not a child of the current process.\n");
        return 0;
    }
    if (process->state != PROCESS_STATE_PAUSED || !process->resumable) {
        print("\nProcess is not paused.\n");
        return 0;
    }

    cleanup_user_process_mapping(process);
    process->resumable = 0;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process_mark_failed(process, PROCESS_TERM_KILLED, 0);
    scheduler_mark_finished(process);
    process->active = 0;

    print("\nKilled user program [pid=");
    print_hex32(process->pid);
    print("].\n");
    return 1;
}

static int set_user_program_background(uint32_t pid, uint32_t enabled) {
    Process* parent = current_process();
    if (parent == 0) {
        print("\nNo current parent process.\n");
        return 0;
    }

    Process* process = pid == 0 ? find_last_paused_child_process(parent->pid) : find_process_by_pid(pid);
    if (process == 0) {
        print("\nProcess not found.\n");
        return 0;
    }
    if (process->parent_pid != parent->pid) {
        print("\nProcess is not a child of the current process.\n");
        return 0;
    }
    if (process->state != PROCESS_STATE_PAUSED || !process->resumable) {
        print("\nProcess is not paused.\n");
        return 0;
    }

    process->background = enabled ? 1 : 0;
    print("\nSet user program [pid=");
    print_hex32(process->pid);
    print("] mode=");
    print(process->background ? "bg" : "fg");
    print(".\n");
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
    char default_program[] = "USHELL_C.ELF";
    command_run(default_program);
}

static void command_ushellc() {
    char default_program[] = "USHELL_C.ELF";
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

extern "C" uint64_t timer_handler64() {
    pit.handle();
    scheduler_wake_sleeping_processes(pit.get_tick());
    scheduler_on_tick();
    if (scheduler_should_preempt_current()) {
        return TIMER_PREEMPT_TO_KERNEL;
    }
    Process* process = current_process();
    if (process != 0 && process->timeslice_ticks == 0) {
        process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    }
    return 0;
}

extern "C" void user_test_interrupt_handler64() {
    user_test_count++;
    print("\nUser mode reached.");
}

extern "C" void user_exit_interrupt_handler64() {
    print("\nUser mode exit requested.");
}

extern "C" uint64_t syscall_dispatch64(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
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
        process_mark_returned(current_process(), PROCESS_TERM_EXIT, (uint32_t)arg1);
        print("\nUser mode exit requested.");
        return SYSCALL_RETURN_TO_KERNEL;
    }

    if (syscall_no == SYS_PUTCHAR) {
        putchar_both((char)(arg1 & 0xFF));
        return 1;
    }

    if (syscall_no == SYS_GETCHAR) {
        while (1) {
            char ascii = 0;
            if (keyboard.try_read_char(&ascii)) {
                return (uint64_t)(unsigned char)ascii;
            }

            if (continue_woken_processes(0)) {
                redraw_user_shell_prompt_if_needed();
                continue;
            }

            if (continue_background_processes(0)) {
                redraw_user_shell_prompt_if_needed();
                continue;
            }

            __asm__ volatile("sti; hlt; cli");
        }
    }

    if (syscall_no == SYS_LIST_FILES) {
        vfs_list_files();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_LIST_FILES_AT) {
        char file_path[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_path, sizeof(file_path))) {
            print("\nInvalid user path pointer.");
            return (uint64_t)-1;
        }

        if (vfs_list_files_at(file_path) != VFS_OK) {
            print("\nFailed to list path: ");
            print(file_path);
            print("\n");
            return (uint64_t)-1;
        }
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_VFS_OPEN) {
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user filename pointer.");
            return (uint64_t)-1;
        }

        return (uint64_t)vfs_open(file_name, (uint32_t)arg2);
    }

    if (syscall_no == SYS_VFS_READ) {
        uint32_t requested = (uint32_t)arg3;
        if (requested == 0) {
            return 0;
        }
        if (requested > 4096) {
            requested = 4096;
        }

        uint8_t* temp = (uint8_t*)kmalloc(requested);
        if (temp == 0) {
            return (uint64_t)-1;
        }

        uint32_t bytes_read = 0;
        int result = vfs_read((int)arg1, temp, requested, &bytes_read);
        if (result != VFS_OK || !copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)arg2, temp, bytes_read)) {
            kfree(temp);
            return (uint64_t)-1;
        }

        kfree(temp);
        return bytes_read;
    }

    if (syscall_no == SYS_VFS_WRITE) {
        uint32_t requested = (uint32_t)arg3;
        if (requested == 0) {
            return 0;
        }
        if (requested > 4096) {
            requested = 4096;
        }

        uint8_t* temp = (uint8_t*)kmalloc(requested);
        if (temp == 0) {
            return (uint64_t)-1;
        }
        if (!copy_user_buffer((const uint8_t*)(uintptr_t)arg2, temp, requested)) {
            kfree(temp);
            return (uint64_t)-1;
        }

        uint32_t bytes_written = 0;
        int result = vfs_write((int)arg1, temp, requested, &bytes_written);
        kfree(temp);
        if (result != VFS_OK) {
            return (uint64_t)-1;
        }
        return bytes_written;
    }

    if (syscall_no == SYS_VFS_CLOSE) {
        return vfs_close((int)arg1) == VFS_OK ? 0 : (uint64_t)-1;
    }

    if (syscall_no == SYS_CAT_FILE) {
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            print("\nInvalid user filename pointer.");
            return (uint64_t)-1;
        }

        VFSFileInfo file_info;
        if (vfs_get_file_info(file_name, &file_info) != VFS_OK) {
            print("\nFile not found: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }

        uint32_t buffer_size = file_info.size + 1;
        if (buffer_size < 512) {
            buffer_size = 512;
        }

        uint8_t* file_buffer = (uint8_t*)kmalloc(buffer_size);
        if (file_buffer == 0) {
            print("\nOut of memory reading file.\n");
            return (uint64_t)-1;
        }

        uint32_t bytes_read = 0;
        if (vfs_read_file(file_name, file_buffer, buffer_size, &bytes_read) != VFS_OK) {
            kfree(file_buffer);
            print("\nFailed to read file: ");
            print(file_name);
            print("\n");
            return (uint64_t)-1;
        }

        file_buffer[bytes_read] = '\0';
        print((const char*)file_buffer);
        if (bytes_read == 0 || file_buffer[bytes_read - 1] != '\n') {
            print("\n");
        }
        kfree(file_buffer);
        return bytes_read;
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

    if (syscall_no == SYS_RM_FILE || syscall_no == SYS_RM_FILE_SILENT) {
        int noisy = syscall_no == SYS_RM_FILE;
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            if (noisy) {
                print("\nInvalid user filename pointer.");
            }
            return (uint64_t)-1;
        }

        if (vfs_delete_file(file_name) == VFS_OK) {
            if (noisy) {
                print("\nDeleted: ");
                print(file_name);
                print("\n");
            }
            return 0;
        }

        if (noisy) {
            print("\nFile not found: ");
            print(file_name);
            print("\n");
        }
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_UPTIME) {
        command_uptime();
        print("\n");
        return 0;
    }

    if (syscall_no == SYS_TOUCH_FILE || syscall_no == SYS_TOUCH_FILE_SILENT) {
        int noisy = syscall_no == SYS_TOUCH_FILE;
        char file_name[32];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            if (noisy) {
                print("\nInvalid user filename pointer.");
            }
            return (uint64_t)-1;
        }

        if (vfs_touch_file(file_name) == VFS_OK) {
            if (noisy) {
                print("\nTouched: ");
                print(file_name);
                print("\n");
            }
            return 0;
        }

        if (noisy) {
            print("\nFailed to touch file: ");
            print(file_name);
            print("\n");
        }
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_SAVE_FILE || syscall_no == SYS_SAVE_FILE_SILENT) {
        int noisy = syscall_no == SYS_SAVE_FILE;
        char file_name[32];
        char file_text[128];
        if (!copy_user_cstring((const char*)(uintptr_t)arg1, file_name, sizeof(file_name))) {
            if (noisy) {
                print("\nInvalid user filename pointer.");
            }
            return (uint64_t)-1;
        }
        if (!copy_user_cstring((const char*)(uintptr_t)arg2, file_text, sizeof(file_text))) {
            if (noisy) {
                print("\nInvalid user text pointer.");
            }
            return (uint64_t)-1;
        }

        if (vfs_write_file(file_name, (uint8_t*)file_text, (uint32_t)strlen64(file_text)) == VFS_OK) {
            if (noisy) {
                print("\nSaved: ");
                print(file_name);
                print("\n");
            }
            return 0;
        }

        if (noisy) {
            print("\nFailed to save file: ");
            print(file_name);
            print("\n");
        }
        return (uint64_t)-1;
    }

    if (syscall_no == SYS_GET_PID) {
        Process* process = current_process();
        return process != 0 ? process->pid : 0;
    }

    if (syscall_no == SYS_GET_PPID) {
        Process* process = current_process();
        return process != 0 ? process->parent_pid : 0;
    }

    if (syscall_no == SYS_PS) {
        print_process_table();
        return 0;
    }

    if (syscall_no == SYS_LAST_STATUS) {
        Process* process = current_process();
        if (process == 0) {
            print("\nNo current user process.\n");
            return (uint64_t)-1;
        }

        const Process* child = find_last_child_process(process->pid);
        if (child == 0) {
            print("\nNo child program result.\n");
            return 0;
        }

        print_child_result_compact("Last child", child);
        print("\n");
        return child->status_code;
    }

    if (syscall_no == SYS_WAIT_CHILD) {
        Process* process = current_process();
        if (process == 0) {
            print("\nNo current user process.\n");
            return (uint64_t)-1;
        }

        Process* child = find_waitable_child_process(process->pid);
        if (child == 0) {
            print("\nNo unreaped child result.\n");
            return 0;
        }

        print_child_result_compact("Wait child", child);
        print("\n");
        child->reaped = 1;
        return child->status_code;
    }

    if (syscall_no == SYS_SCHED_INFO) {
        print_scheduler_info();
        return 0;
    }

    if (syscall_no == SYS_VFS_MOUNTS) {
        print_vfs_mounts();
        return 0;
    }

    if (syscall_no == SYS_YIELD) {
        return SYSCALL_YIELD_TO_KERNEL;
    }

    if (syscall_no == SYS_RESUME_USER) {
        uint32_t pid = (uint32_t)arg1;
        if (!resume_user_program(pid)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_KILL_USER) {
        uint32_t pid = (uint32_t)arg1;
        if (!kill_user_program(pid)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_REAP_ALL_CHILDREN) {
        Process* process = current_process();
        if (process == 0) {
            print("\nNo current user process.\n");
            return (uint64_t)-1;
        }

        uint32_t count = reap_all_child_processes(process->pid);
        print("\nReaped children: ");
        print_hex32(count);
        print("\n");
        return count;
    }

    if (syscall_no == SYS_JOBS) {
        Process* process = current_process();
        print_jobs_for_process(process);
        return 0;
    }

    if (syscall_no == SYS_SLEEP) {
        uint32_t ticks = (uint32_t)arg1;
        if (ticks == 0) {
            ticks = 1;
        }
        return SYSCALL_SLEEP_TO_KERNEL;
    }

    if (syscall_no == SYS_SET_BACKGROUND) {
        uint32_t pid = (uint32_t)arg1;
        uint32_t enabled = (uint32_t)arg2;
        if (!set_user_program_background(pid, enabled)) {
            return (uint64_t)-1;
        }
        return 0;
    }

    if (syscall_no == SYS_CHILDREN_ACTIVE) {
        Process* process = current_process();
        if (process == 0) {
            return 0;
        }
        return count_unfinished_child_processes(process->pid);
    }

    if (syscall_no == SYS_REAP_ALL_CHILDREN_SILENT) {
        Process* process = current_process();
        if (process == 0) {
            return 0;
        }
        return reap_all_child_processes(process->pid);
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
    vfs_init();
    vfs_mount_fat12_root(&fat);
    vfs_mount_memfs("/mem");
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
