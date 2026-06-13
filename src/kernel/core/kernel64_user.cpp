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
    if (bytes_read >= sizeof(uint64_t) && ((const DrvHeader*)program_buffer)->magic == DRV_MAGIC) {
        process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 8);
        scheduler_mark_finished(process);
        print("\nCannot run DRV package as a user program. Use drvload: ");
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
