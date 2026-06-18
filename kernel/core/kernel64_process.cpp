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
