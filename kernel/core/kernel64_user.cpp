static void set_user_fs_base(uint64_t base) {
    uint32_t low = (uint32_t)base;
    uint32_t high = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" : : "c"(0xC0000100u), "a"(low), "d"(high));
}

static void focus_foreground_process(Process* process) {
    if (process != 0 && !process->background) {
        if (process_focused_pid() != process->pid) {
            user_input_reset();
        }
        process_set_focus(process->pid);
    }
}

static int parent_has_ready_context(const Process* parent) {
    return parent != 0 &&
           parent->active &&
           parent->state == PROCESS_STATE_PAUSED &&
           parent->resumable &&
           !parent->wait_pending &&
           parent->wait_reason != PROCESS_WAIT_NONE;
}

static int resume_saved_parent(Process* parent) {
    if (!parent_has_ready_context(parent)) {
        return 0;
    }
    Process* grandparent = parent->parent_pid != 0
        ? find_process_by_pid(parent->parent_pid)
        : 0;
    return resume_user_program_internal(grandparent, parent, 0);
}

static uint32_t user_region_rights_from_vm_flags(uint64_t flags) {
    uint32_t rights = ADDRESS_SPACE_REGION_READ;
    if (flags & VM_FLAG_WRITABLE) {
        rights |= ADDRESS_SPACE_REGION_WRITE;
    }
    if (!(flags & VM_FLAG_NO_EXECUTE)) {
        rights |= ADDRESS_SPACE_REGION_EXECUTE;
    }
    return rights;
}

static int run_user_program_internal(const char* command_line, uint32_t permissions) {
    if (command_line == 0 || command_line[0] == '\0') {
        print("\nUser program filename is empty.");
        return 0;
    }

    if (process_execution_depth() >= USER_PROGRAM_SLOT_COUNT) {
        print("\nUser program nesting limit reached.");
        return 0;
    }

    uint32_t slot_index = 0;
    if (!allocate_execution_slot(&slot_index)) {
        print("\nNo free execution slot. Resume or finish paused programs first.");
        return 0;
    }
    uint32_t stack_index = process_execution_depth();
    uint64_t user_code_base = 0;
    uint64_t user_stack_guard_base = 0;
    get_execution_slot_bases(slot_index, &user_code_base, &user_stack_guard_base);
    uint64_t user_stack_base = user_stack_guard_base +
        ((uint64_t)USER_STACK_GUARD_PAGE_COUNT * VM_PAGE_SIZE);
    uint64_t user_stack_top = user_stack_base + ((uint64_t)USER_STACK_PAGE_COUNT * VM_PAGE_SIZE);
    Process* parent = current_process();
    Process* process = allocate_process_record();
    if (process == 0) {
        reap_all_child_processes(parent != 0 ? parent->pid : 0);
        process = allocate_process_record();
    }
    if (process == 0) {
        print("\nProcess table is full. Finish or reap child programs first.");
        return 0;
    }
    process_assign_identity(process, next_pid++, parent);
    Thread* thread = process_main_thread(process);
    if (process->pid == 0 || thread == 0) {
        print("\nFailed to allocate the process main thread.");
        return 0;
    }
    process->permissions = permissions;
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
    if (!address_space_ensure_root(&process->address_space)) {
        process_mark_failed(process, PROCESS_TERM_MEMORY_ERROR, 4);
        scheduler_mark_finished(process);
        print("\nFailed to create user address space.");
        return 0;
    }
    process->code_base = user_code_base;
    process->elf_link_base = 0;
    process->stack_guard_base = user_stack_guard_base;
    process->stack_base = user_stack_base;
    process->heap_base = user_code_base + USER_HEAP_OFFSET;
    process->heap_break = process->heap_base;
    process->heap_mapped_end = process->heap_base;
    const uint64_t secondary_stack_slot_span =
        (uint64_t)(1U + (OS_THREAD_STACK_MAX / VM_PAGE_SIZE)) * VM_PAGE_SIZE;
    process->heap_limit = user_code_base + USER_SLOT_SPAN -
        (uint64_t)(THREADS_PER_PROCESS_MAX - 1) * secondary_stack_slot_span;
    process->heap_page_count = 0;
    process->stack_guard_page_count = USER_STACK_GUARD_PAGE_COUNT;
    process->stack_page_count = 0;
    process->address_space.code_base = process->code_base;
    process->address_space.elf_link_base = process->elf_link_base;
    process->address_space.stack_guard_base = process->stack_guard_base;
    process->address_space.stack_base = process->stack_base;
    process->address_space.heap_base = process->heap_base;
    process->address_space.heap_break = process->heap_break;
    process->address_space.heap_mapped_end = process->heap_mapped_end;
    process->address_space.heap_limit = process->heap_limit;
    process->address_space.stack_guard_page_count = process->stack_guard_page_count;
    process->entry_point = user_code_base;
    process->state = PROCESS_STATE_LOADED;
    process->termination_reason = PROCESS_TERM_NONE;
    process->status_code = 0;
    process->active = 1;

    VFSFileInfo file_info;
    if (vfs_get_file_info(filename, &file_info) != VFS_OK) {
        process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 1);
        scheduler_mark_finished(process);
        print("\nUser program not found: ");
        print(filename);
        print("\n");
        return 0;
    }

    uint32_t max_user_image_size = (uint32_t)(user_stack_guard_base - user_code_base);
    if (file_info.size == 0 || file_info.size > max_user_image_size) {
        process->image_size = file_info.size;
        process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 2);
        scheduler_mark_finished(process);
        print("\nUser program size is invalid for the current loader.\n");
        return 0;
    }
    process->image_size = file_info.size;
    process->elf_alias_page_count = 0;
    process->elf_alias_ready = 0;

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

        if ((elf_first_vaddr & (VM_PAGE_SIZE - 1ULL)) != 0) {
            process_mark_failed(process, PROCESS_TERM_LOAD_ERROR, 6);
            scheduler_mark_finished(process);
            print("\nELF64 first load address must be page aligned: ");
            print(filename);
            print("\n");
            kfree(program_buffer);
            return 0;
        }

        code_page_count = (uint32_t)((elf_load_size + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE);
        process->entry_point = user_code_base + (elf_header->e_entry - elf_first_vaddr);
        process->elf_link_base = elf_first_vaddr;
        process->elf_alias_page_count = code_page_count;
        is_elf_image = 1;
    } else {
        if (file_info.size > VM_PAGE_SIZE) {
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
            elf_page_flags[i] = VM_FLAG_USER;
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
            uint32_t first_page = (uint32_t)(seg_start / VM_PAGE_SIZE);
            uint32_t last_page = (uint32_t)((seg_end - 1) / VM_PAGE_SIZE);
            uint64_t final_flags = elf64_segment_page_flags(phdr->p_flags);
            for (uint32_t page = first_page; page <= last_page; page++) {
                elf_page_flags[page] |= (final_flags & VM_FLAG_WRITABLE);
                if (final_flags & VM_FLAG_WRITABLE) {
                    elf_page_flags[page] |= VM_FLAG_NO_EXECUTE;
                }
            }
        }
    }

    uint32_t mapped_code_pages = 0;
    for (uint32_t page = 0; page < code_page_count; page++) {
        uint64_t code_phys = (uint64_t)(uintptr_t)pmm_alloc_block();
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

        uint64_t virt = user_code_base + ((uint64_t)page * VM_PAGE_SIZE);
        if (!address_space_map_page(&process->address_space,
                                    virt,
                                    code_phys,
                                    VM_FLAG_WRITABLE | VM_FLAG_USER | VM_FLAG_NO_EXECUTE)) {
            pmm_free_block((void*)(uintptr_t)code_phys);
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

        for (uint64_t i = 0; i < VM_PAGE_SIZE; i++) {
            *((volatile uint8_t*)(uintptr_t)(code_phys + i)) = 0;
        }
        mapped_code_pages++;
    }
    process->code_page_count = code_page_count;
    process->address_space.code_page_count = process->code_page_count;

    for (uint32_t page = 0; page < USER_STACK_PAGE_COUNT; page++) {
        uint64_t stack_phys = (uint64_t)(uintptr_t)pmm_alloc_block();
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

        uint64_t virt = user_stack_base + ((uint64_t)page * VM_PAGE_SIZE);
        if (!address_space_map_page(&process->address_space,
                                    virt,
                                    stack_phys,
                                    VM_FLAG_WRITABLE | VM_FLAG_USER | VM_FLAG_NO_EXECUTE)) {
            pmm_free_block((void*)(uintptr_t)stack_phys);
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

        for (uint64_t i = 0; i < VM_PAGE_SIZE; i++) {
            *((volatile uint8_t*)(uintptr_t)(stack_phys + i)) = 0;
        }
        process->stack_page_count = page + 1;
    }
    process->address_space.stack_page_count = process->stack_page_count;
    if (!address_space_add_region(&process->address_space,
                                  process->stack_base,
                                  (uint64_t)process->stack_page_count * VM_PAGE_SIZE,
                                  ADDRESS_SPACE_REGION_READ | ADDRESS_SPACE_REGION_WRITE)) {
        cleanup_user_process_mapping(process);
        process->code_page_count = 0;
        process->stack_page_count = 0;
        kfree(program_buffer);
        if (elf_page_flags != 0) {
            kfree(elf_page_flags);
        }
        process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 8);
        scheduler_mark_finished(process);
        print("\nFailed to record user stack region.");
        return 0;
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
                if (!write_user_byte_phys(process, dest + j, program_buffer[phdr->p_offset + j])) {
                    cleanup_user_process_mapping(process);
                    process->code_page_count = 0;
                    kfree(program_buffer);
                    kfree(elf_page_flags);
                    process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 4);
                    scheduler_mark_finished(process);
                    print("\nFailed to fill ELF user segment.");
                    return 0;
                }
            }
            for (uint64_t j = phdr->p_filesz; j < phdr->p_memsz; j++) {
                if (!write_user_byte_phys(process, dest + j, 0)) {
                    cleanup_user_process_mapping(process);
                    process->code_page_count = 0;
                    kfree(program_buffer);
                    kfree(elf_page_flags);
                    process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 5);
                    scheduler_mark_finished(process);
                    print("\nFailed to clear ELF user segment.");
                    return 0;
                }
            }
        }

        for (uint32_t page = 0; page < code_page_count; page++) {
            uint64_t virt = user_code_base + ((uint64_t)page * VM_PAGE_SIZE);
            uint64_t phys = address_space_get_phys(&process->address_space, virt);
            if (phys == 0 ||
                !address_space_map_page(&process->address_space,
                                        virt,
                                        phys & 0x000FFFFFFFFFF000ULL,
                                        elf_page_flags[page])) {
                cleanup_user_process_mapping(process);
                process->code_page_count = 0;
                kfree(program_buffer);
                kfree(elf_page_flags);
                process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 3);
                scheduler_mark_finished(process);
                print("\nFailed to apply ELF page permissions.");
                return 0;
            }
            if (!address_space_add_region(&process->address_space,
                                          virt,
                                          VM_PAGE_SIZE,
                                          user_region_rights_from_vm_flags(elf_page_flags[page]))) {
                cleanup_user_process_mapping(process);
                process->code_page_count = 0;
                kfree(program_buffer);
                kfree(elf_page_flags);
                process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 6);
                scheduler_mark_finished(process);
                print("\nFailed to record ELF user region.");
                return 0;
            }
        }
        kfree(elf_page_flags);
    } else {
        for (uint32_t i = 0; i < file_info.size; i++) {
            if (!write_user_byte_phys(process, user_code_base + i, program_buffer[i])) {
                cleanup_user_process_mapping(process);
                process->code_page_count = 0;
                kfree(program_buffer);
                process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 7);
                scheduler_mark_finished(process);
                print("\nFailed to fill flat user program.");
                return 0;
            }
        }
        if (!address_space_protect_range(&process->address_space,
                                         user_code_base,
                                         VM_PAGE_SIZE,
                                         VM_FLAG_USER)) {
            cleanup_user_process_mapping(process);
            process->code_page_count = 0;
            kfree(program_buffer);
            process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 3);
            scheduler_mark_finished(process);
            print("\nFailed to apply flat user page permissions.");
            return 0;
        }
        if (!address_space_add_region(&process->address_space,
                                      user_code_base,
                                      VM_PAGE_SIZE,
                                      ADDRESS_SPACE_REGION_READ | ADDRESS_SPACE_REGION_EXECUTE)) {
            cleanup_user_process_mapping(process);
            process->code_page_count = 0;
            kfree(program_buffer);
            process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 6);
            scheduler_mark_finished(process);
            print("\nFailed to record flat user region.");
            return 0;
        }
    }
    kfree(program_buffer);

    process->state = PROCESS_STATE_LOADED;

    uint64_t saved_rsp0 = gdt64_get_kernel_stack();
    int saved_keyboard_mask = interrupt_controller_irq_masked(1);
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
    interrupt_controller_set_mask(1, 0);
    gdt64_set_kernel_stack(thread->context->kernel_stack_base + VM_PAGE_SIZE);
    if (!map_user_elf_alias(process)) {
        cleanup_user_process_mapping(process);
        process->code_page_count = 0;
        process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 9);
        scheduler_mark_finished(process);
        print("\nFailed to map user ELF link-address alias.");
        return 0;
    }
    process->state = PROCESS_STATE_RUNNING;
    if (parent != 0 && !process->background) {
        process_wait_begin(parent, PROCESS_WAIT_CHILD, 0, 0, 0);
    }
    scheduler_mark_running(process);
    focus_foreground_process(process);
    if (!process_execution_push(process, thread, &stack_index)) {
        process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 10);
        scheduler_mark_finished(process);
        return 0;
    }
    address_space_activate(&process->address_space);
    uint64_t initial_user_rsp = prepare_user_stack_with_argv(process, user_stack_top, &launch);
    restore_thread_fx_state64(thread);
    cpu_local_current()->user_state.return_reason = PROCESS_PAUSE_NONE;
    enter_user_mode(process->entry_point, initial_user_rsp);
    const uint32_t initial_return_reason =
        cpu_local_current()->user_state.return_reason;
    process_execution_pop(stack_index, process, thread);
    Process* active_after_return = current_process();
    if (active_after_return != 0) {
        address_space_activate(&active_after_return->address_space);
    } else {
        address_space_activate_kernel();
    }

    interrupt_controller_set_mask(1, saved_keyboard_mask);
    gdt64_set_kernel_stack(saved_rsp0);
    kernel_user_return_rsp = saved_return_rsp;
    kernel_user_saved_rbx = saved_rbx;
    kernel_user_saved_rbp = saved_rbp;
    kernel_user_saved_r12 = saved_r12;
    kernel_user_saved_r13 = saved_r13;
    kernel_user_saved_r14 = saved_r14;
    kernel_user_saved_r15 = saved_r15;

    /*
     * Process::state is a compatibility summary and another thread may
     * legitimately change it while the main thread is returning from a local
     * preemption. The execution decision belongs to the returning CPU's
     * fixed return reason.
     */
    if (initial_return_reason != PROCESS_PAUSE_NONE) {
        if (parent != 0 && parent->active) {
            if (parent_has_ready_context(parent)) {
                return resume_saved_parent(parent);
            }
            if (parent->state != PROCESS_STATE_PAUSED || !parent->resumable) {
                if (!map_user_elf_alias(parent)) {
                    process_mark_failed(parent, PROCESS_TERM_MAP_ERROR, 9);
                    scheduler_mark_finished(parent);
                    return 0;
                }
                scheduler_mark_running(parent);
                focus_foreground_process(parent);
                return 1;
            }
        }
        if (parent == 0 && !process->background) {
            return wait_for_terminal_process(process);
        }
        if (continue_ready_threads(thread_identity(thread))) {
            return 1;
        }
        if (parent == 0 &&
            initial_return_reason == PROCESS_PAUSE_YIELD) {
            return continue_ready_threads(thread_identity(thread)) ? 1 : 1;
        }
        if (parent == 0 &&
            (initial_return_reason == PROCESS_PAUSE_SLEEP ||
             initial_return_reason == PROCESS_PAUSE_WAIT)) {
            return 1;
        }
        return 1;
    }

    if (!process->background) {
        user_input_reset();
    }
    if (thread->exited) {
        thread_release_runtime(thread);
    }
    cleanup_user_process_mapping(process);
    process->code_page_count = 0;

    if (process->state != PROCESS_STATE_FAILED && process->state != PROCESS_STATE_RETURNED) {
        process_mark_returned(process, PROCESS_TERM_NONE, 0);
    }
    process->resumable = 0;
    scheduler_mark_finished(process);
    if (parent != 0 && parent->active) {
        if (parent_has_ready_context(parent)) {
            return resume_saved_parent(parent);
        }
        if (!map_user_elf_alias(parent)) {
            process_mark_failed(parent, PROCESS_TERM_MAP_ERROR, 9);
            scheduler_mark_finished(parent);
            return 0;
        }
        scheduler_mark_running(parent);
        focus_foreground_process(parent);
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

    if (continue_ready_threads(thread_identity(thread))) {
        return 1;
    }

    if (parent == 0 || !parent->active) {
        return 1;
    }

    if (parent_should_resume_immediately(parent)) {
        if (parent_has_ready_context(parent)) {
            return resume_saved_parent(parent);
        }
        if (!map_user_elf_alias(parent)) {
            process_mark_failed(parent, PROCESS_TERM_MAP_ERROR, 9);
            scheduler_mark_finished(parent);
            return 0;
        }
        scheduler_mark_running(parent);
        focus_foreground_process(parent);
        return 1;
    }
    return 1;
}

int run_user_program(const char* command_line) {
    return run_user_program_internal(command_line, OS_PROCESS_PERMISSION_ALL);
}

int run_user_program_with_permissions(const char* command_line, uint32_t permissions) {
    if ((permissions & ~OS_PROCESS_PERMISSION_VALID_MASK) != 0) {
        return 0;
    }
    return run_user_program_internal(command_line, permissions);
}

static int resume_user_program_internal(Process* parent, Process* process, int print_banner) {
    return resume_user_thread_internal(parent,
                                       process,
                                       process_main_thread(process),
                                       print_banner);
}

static int resume_user_thread_internal(Process* parent,
                                       Process* process,
                                       Thread* thread,
                                       int print_banner) {
    if (process == 0 || thread == 0 || thread->context == 0 ||
        thread->owner != process) {
        return 0;
    }
    if (process_execution_depth() >= EXECUTION_STACK_SIZE) {
        return 0;
    }
    ThreadContext* context = thread->context;
    uint32_t stack_index = process_execution_depth();

    uint64_t saved_rsp0 = gdt64_get_kernel_stack();
    int saved_keyboard_mask = interrupt_controller_irq_masked(1);
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

    kernel_user_resume_rbx = context->saved_rbx;
    kernel_user_resume_rcx = context->saved_rcx;
    kernel_user_resume_rdx = context->saved_rdx;
    kernel_user_resume_rbp = context->saved_rbp;
    kernel_user_resume_rsi = context->saved_rsi;
    kernel_user_resume_rdi = context->saved_rdi;
    kernel_user_resume_r8 = context->saved_r8;
    kernel_user_resume_r9 = context->saved_r9;
    kernel_user_resume_r10 = context->saved_r10;
    kernel_user_resume_r11 = context->saved_r11;
    kernel_user_resume_r12 = context->saved_r12;
    kernel_user_resume_r13 = context->saved_r13;
    kernel_user_resume_r14 = context->saved_r14;
    kernel_user_resume_r15 = context->saved_r15;
    kernel_user_resume_rip = context->saved_rip;
    kernel_user_resume_rsp = context->saved_rsp;
    kernel_user_resume_rflags = context->saved_rflags;

    interrupt_controller_set_mask(1, 0);
    gdt64_set_kernel_stack(context->kernel_stack_base + VM_PAGE_SIZE);
    if (!map_user_elf_alias(process)) {
        process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 9);
        scheduler_mark_finished(process);
        print("\nFailed to map user ELF link-address alias.");
        return 0;
    }
    if (!process_execution_push(process, thread, &stack_index)) {
        process_mark_failed(process, PROCESS_TERM_MAP_ERROR, 10);
        scheduler_mark_finished(process);
        return 0;
    }
    address_space_activate(&process->address_space);
    complete_waiting_syscall64(thread);
    kernel_user_resume_rax = context->saved_rax;
    if (parent != 0 && parent->active && !process->background) {
        process_wait_begin(parent, PROCESS_WAIT_CHILD, 0, 0, 0);
    }
    scheduler_mark_thread_running(thread);
    focus_foreground_process(process);
    process->state = PROCESS_STATE_RUNNING;
    context->resumable = 0;
    set_user_fs_base(context->tls_base);
    restore_thread_fx_state64(thread);
    cpu_local_current()->user_state.return_reason = PROCESS_PAUSE_NONE;
    resume_user_mode();
    const uint32_t return_reason =
        cpu_local_current()->user_state.return_reason;
    set_user_fs_base(0);
    process_execution_pop(stack_index, process, thread);
    Process* active_after_return = current_process();
    if (active_after_return != 0) {
        address_space_activate(&active_after_return->address_space);
    } else {
        address_space_activate_kernel();
    }

    interrupt_controller_set_mask(1, saved_keyboard_mask);
    gdt64_set_kernel_stack(saved_rsp0);
    kernel_user_return_rsp = saved_return_rsp;
    kernel_user_saved_rbx = saved_rbx;
    kernel_user_saved_rbp = saved_rbp;
    kernel_user_saved_r12 = saved_r12;
    kernel_user_saved_r13 = saved_r13;
    kernel_user_saved_r14 = saved_r14;
    kernel_user_saved_r15 = saved_r15;

    if (thread->exited && process->active && !process->exiting) {
        ThreadIdentity completed = thread_identity(thread);
        thread_release_runtime(thread);
        if (continue_ready_threads(completed)) {
            return 1;
        }
        if (parent_should_resume_immediately(parent)) {
            return resume_saved_parent(parent) ? 1 : 1;
        }
        return 1;
    }

    if (return_reason != PROCESS_PAUSE_NONE) {
        if (parent != 0 && parent->active) {
            if (parent_has_ready_context(parent)) {
                return resume_saved_parent(parent);
            }
            if (parent->state != PROCESS_STATE_PAUSED || !parent->resumable) {
                if (!map_user_elf_alias(parent)) {
                    process_mark_failed(parent, PROCESS_TERM_MAP_ERROR, 9);
                    scheduler_mark_finished(parent);
                    return 0;
                }
                scheduler_mark_running(parent);
                focus_foreground_process(parent);
                return 1;
            }
        }
        if (parent == 0 && !process->background) {
            return wait_for_terminal_process(process);
        }
        if (continue_ready_threads(thread_identity(thread))) {
            return 1;
        }
        if (parent == 0 && return_reason == PROCESS_PAUSE_YIELD) {
            return continue_ready_threads(thread_identity(thread)) ? 1 : 1;
        }
        if (parent == 0 &&
            (return_reason == PROCESS_PAUSE_SLEEP ||
             return_reason == PROCESS_PAUSE_WAIT)) {
            return 1;
        }
        return 1;
    }

    if (!process->background) {
        user_input_reset();
    }
    if (thread->exited) {
        thread_release_runtime(thread);
    }
    cleanup_user_process_mapping(process);
    process->code_page_count = 0;
    if (process->state != PROCESS_STATE_FAILED && process->state != PROCESS_STATE_RETURNED) {
        process_mark_returned(process, PROCESS_TERM_NONE, 0);
    }
    context->resumable = 0;
    scheduler_mark_finished(process);
    if (parent_should_resume_immediately(parent)) {
        if (parent_has_ready_context(parent)) {
            return resume_saved_parent(parent);
        }
        if (!map_user_elf_alias(parent)) {
            process_mark_failed(parent, PROCESS_TERM_MAP_ERROR, 9);
            scheduler_mark_finished(parent);
            return 0;
        }
        scheduler_mark_running(parent);
        focus_foreground_process(parent);
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

    if (continue_ready_threads(thread_identity(thread))) {
        return 1;
    }

    if (parent == 0 || !parent->active) {
        return 1;
    }

    if (parent_should_resume_immediately(parent)) {
        if (parent_has_ready_context(parent)) {
            return resume_saved_parent(parent);
        }
        scheduler_mark_running(parent);
        focus_foreground_process(parent);
        return 1;
    }
    if (nested_syscall_waiter_active(process)) {
        return 1;
    }
    return 1;
}

int scheduler_execute_claimed_thread(Thread* thread) {
    if (thread == 0 || thread->owner == 0 ||
        thread->running_cpu == THREAD_CPU_INVALID ||
        thread->context == 0 ||
        thread->context->scheduler_state != SCHED_STATE_RUNNING) {
        return 0;
    }
    Process* process = thread->owner;
    Process* parent = process->parent_pid != 0
        ? find_process_by_pid(process->parent_pid) : 0;
    return resume_user_thread_internal(parent, process, thread, 0);
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
    if (process_wait_is_pending(process)) {
        print("\nProcess is waiting.\n");
        return 0;
    }
    if (!process->resumable || process->state != PROCESS_STATE_PAUSED) {
        print("\nProcess is not paused.\n");
        return 0;
    }
    if (process_execution_depth() >= USER_PROGRAM_SLOT_COUNT) {
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
    if (process->background) {
        if (process_focused_pid() == process->pid) {
            user_input_reset();
        }
        process_clear_focus(process->pid);
    } else {
        focus_foreground_process(process);
    }
    print("\nSet user program [pid=");
    print_hex32(process->pid);
    print("] mode=");
    print(process->background ? "bg" : "fg");
    print(".\n");
    return 1;
}
