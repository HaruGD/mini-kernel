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
#include "fs/fat32.h"
#include "fs/vfs.h"
#include "kernel/boot_info.h"
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
ATADriver fat32_ata(0xF0);
KeyboardDriver keyboard;
PIT pit;
FAT12Driver fat(&ata);
FAT32Driver fat32(&fat32_ata);

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

int resume_user_program(uint32_t pid);

static void save_paused_context64(uint64_t* frame, Process* process, uint32_t pause_reason, uint64_t saved_rax) {
    if (process == 0 || frame == 0) {
        return;
    }

    // Frame layout comes from PUSH_GPRS in idt64.asm:
    // [0]=r15 ... [13]=rbx [14]=rax [15]=rip [16]=cs [17]=rflags [18]=rsp [19]=ss
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
    process->saved_rax = saved_rax;
    process->saved_rip = frame[15];
    process->saved_rflags = frame[17];
    process->saved_rsp = frame[18];
    process->state = PROCESS_STATE_PAUSED;
    process->resumable = 1;
    process->pause_reason = (uint8_t)pause_reason;
}

extern "C" void save_yield_context64(uint64_t* frame) {
    Process* process = current_process();
    if (process == 0 || frame == 0) {
        return;
    }

    // Yield/sleep are syscall-driven pauses, so they resume as if the syscall returned 0.
    save_paused_context64(frame, process, PROCESS_PAUSE_YIELD, 0);
    scheduler_yield_current();
}

extern "C" void save_preempt_context64(uint64_t* frame) {
    Process* process = current_process();
    if (process == 0 || frame == 0) {
        return;
    }

    // Timer preemption should preserve the interrupted register state, including rax.
    save_paused_context64(frame, process, PROCESS_PAUSE_PREEMPT, frame[14]);
    scheduler_yield_current();
}

extern "C" void save_sleep_context64(uint64_t* frame, uint32_t sleep_ticks) {
    Process* process = current_process();
    if (process == 0 || frame == 0) {
        return;
    }

    save_paused_context64(frame, process, PROCESS_PAUSE_SLEEP, 0);
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

void redraw_user_shell_prompt_if_needed() {
    const char* prompt = current_process_shell_prompt();
    if (prompt == 0) {
        return;
    }

    print(prompt);
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

int continue_woken_processes(uint32_t exclude_pid) {
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

int continue_background_processes(uint32_t exclude_pid) {
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

static void user_input_reset() {
    char discarded = 0;
    while (keyboard.try_read_char(&discarded)) {
    }
}

void print_boot_info() {
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

void command_memstat() {
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

int run_user_program(const char* command_line) {
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
    uint64_t user_stack_top = user_stack_base + ((uint64_t)USER_STACK_PAGE_COUNT * PAGING64_PAGE_SIZE);
    Process* parent = current_process();
    Process* process = allocate_process_record();
    if (process == 0) {
        print("\nProcess table is full. Reap finished child results with wait.");
        return 0;
    }
    process->pid = next_pid++;
    process->parent_pid = parent != 0 ? parent->pid : 0;
    process->slot_index = slot_index;
    process_copy_cwd(process, parent != 0 ? process_get_cwd(parent) : "/");
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
    process->stack_page_count = 0;
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
            if (phdr->p_memsz == 0 && phdr->p_filesz == 0) {
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

    for (uint32_t page = 0; page < USER_STACK_PAGE_COUNT; page++) {
        uint64_t stack_phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
        if (stack_phys == 0) {
            cleanup_user_process_mapping(process);
            process->code_page_count = 0;
            process->stack_page_count = 0;
            kfree(program_buffer);
            if (elf_page_flags != 0) {
                kfree(elf_page_flags);
            }
            process_mark_failed(process, PROCESS_TERM_MEMORY_ERROR, 2);
            scheduler_mark_finished(process);
            print("\nFailed to allocate user program pages.");
            return 0;
        }

        uint64_t virt = user_stack_base + ((uint64_t)page * PAGING64_PAGE_SIZE);
        if (!paging64_map_page(virt, stack_phys, PAGING64_FLAG_WRITABLE | PAGING64_FLAG_USER)) {
            pmm64_free_block((void*)(uintptr_t)stack_phys);
            cleanup_user_process_mapping(process);
            process->code_page_count = 0;
            process->stack_page_count = 0;
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
            *((volatile uint8_t*)(uintptr_t)(virt + i)) = 0;
        }
        process->stack_page_count = page + 1;
    }

    if (is_elf_image) {
        const Elf64_Phdr* phdrs = (const Elf64_Phdr*)(const void*)(program_buffer + elf_header->e_phoff);
        for (uint16_t i = 0; i < elf_header->e_phnum; i++) {
            const Elf64_Phdr* phdr = &phdrs[i];
            if (phdr->p_type != ELF64_PT_LOAD) {
                continue;
            }
            if (phdr->p_memsz == 0 && phdr->p_filesz == 0) {
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

int resume_user_program(uint32_t pid) {
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

int kill_user_program(uint32_t pid) {
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

int set_user_program_background(uint32_t pid, uint32_t enabled) {
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

void command_version() {
    print("\n[OS-Kernel] v0.0.x (64-bit Long Mode, C++)");
    print("\nBIOS stage1/stage2 + FAT12 loader + IDT/IRQ");
}

void command_uptime() {
    print("\nTick: ");
    print_hex32(pit.get_tick());
    print("\nTSC delta: ");
    print_hex64(read_tsc() - boot_tsc);
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

const BootInfo* kernel_boot_info() {
    return g_boot_info;
}

uint64_t kernel_boot_tsc() {
    return boot_tsc;
}

uint32_t kernel_user_test_count() {
    return user_test_count;
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
    fat32_ata.init();
    fat.init();
    fat32.init();
    vfs_init();
    vfs_mount_fat12_root(&fat);
    vfs_mount_memfs("/mem");
    vfs_mount_fat32("/fat32", &fat32);
    idt64_init();
    keyboard.init();
    pit.init();
    __asm__ volatile("sti");

    print("Memory ready\n");
    print("GDT/TSS ready\n");
    print("Interrupts ready\n");
    print(kernel_shell_prompt());

    while (1) {
        __asm__ volatile("hlt");
    }
}
