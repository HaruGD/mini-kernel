static void save_paused_context64(uint64_t* frame,
                                  Thread* thread,
                                  uint32_t pause_reason,
                                  uint64_t saved_rax) {
    if (thread == 0 || thread->context == 0 || frame == 0) {
        return;
    }
    ThreadContext* context = thread->context;
    __asm__ volatile("fxsave64 %0"
                     : "=m"(context->fx_state)
                     :
                     : "memory");
    context->fx_initialized = 1;

    // Frame layout comes from PUSH_GPRS in idt64.asm:
    // [0]=r15 ... [13]=rbx [14]=rax [15]=rip [16]=cs [17]=rflags [18]=rsp [19]=ss
    context->saved_r15 = frame[0];
    context->saved_r14 = frame[1];
    context->saved_r13 = frame[2];
    context->saved_r12 = frame[3];
    context->saved_r11 = frame[4];
    context->saved_r10 = frame[5];
    context->saved_r9  = frame[6];
    context->saved_r8  = frame[7];
    context->saved_rdi = frame[8];
    context->saved_rsi = frame[9];
    context->saved_rbp = frame[10];
    context->saved_rdx = frame[11];
    context->saved_rcx = frame[12];
    context->saved_rbx = frame[13];
    context->saved_rax = saved_rax;
    context->saved_rip = frame[15];
    context->saved_rflags = frame[17];
    context->saved_rsp = frame[18];
    context->resumable = 1;
    context->pause_reason = (uint8_t)pause_reason;
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        local->user_state.return_reason = pause_reason;
    }
    if (thread->is_main && thread->owner != 0) {
        thread->owner->state = PROCESS_STATE_PAUSED;
    }
}

static void restore_thread_fx_state64(Thread* thread) {
    if (thread == 0 || thread->context == 0) {
        return;
    }
    ThreadContext* context = thread->context;
    if (context->fx_initialized) {
        __asm__ volatile("fxrstor64 %0"
                         :
                         : "m"(context->fx_state)
                         : "memory");
        return;
    }
    __asm__ volatile("fninit" : : : "memory");
}

extern "C" void save_yield_context64(uint64_t* frame) {
    Thread* thread = current_thread();
    if (thread == 0 || frame == 0) {
        return;
    }

    // Yield/sleep are syscall-driven pauses, so they resume as if the syscall returned 0.
    save_paused_context64(frame, thread, PROCESS_PAUSE_YIELD, 0);
    scheduler_yield_current();
}

extern "C" void save_preempt_context64(uint64_t* frame) {
    Thread* thread = current_thread();
    if (thread == 0 || frame == 0) {
        return;
    }

    // Timer preemption should preserve the interrupted register state, including rax.
    save_paused_context64(frame, thread, PROCESS_PAUSE_PREEMPT, frame[14]);
    scheduler_yield_current();
}

extern "C" void save_sleep_context64(uint64_t* frame, uint32_t sleep_ticks) {
    Thread* thread = current_thread();
    Process* process = current_process();
    if (thread == 0 || process == 0 || frame == 0) {
        return;
    }

    save_paused_context64(frame, thread, PROCESS_PAUSE_SLEEP, 0);
    process_wait_begin(process,
                       PROCESS_WAIT_TIMER,
                       0,
                       sleep_ticks,
                       pit.get_tick());
}

extern "C" void save_wait_context64(uint64_t* frame) {
    Thread* thread = current_thread();
    Process* process = current_process();
    if (thread == 0 || process == 0 || frame == 0 ||
        thread->context->wait_reason == PROCESS_WAIT_NONE) {
        return;
    }

    save_paused_context64(frame, thread, PROCESS_PAUSE_WAIT, 0);
    // A timer, IPC sender, or input IRQ may complete the wait after the
    // syscall arms it but before this interrupt-exit path saves the user
    // context. In that case the signal could not enqueue a still-running
    // process. Commit the saved context to READY here; duplicate enqueue is
    // harmless and suppressed by the scheduler queue.
    if (!process_wait_is_pending(process)) {
        scheduler_enqueue_thread(thread);
    }
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
    uint64_t code = USER_SLOT0_CODE_BASE +
        (uint64_t)slot_index * USER_SLOT_SPAN;
    uint64_t stack = USER_SLOT0_STACK_BASE +
        (uint64_t)slot_index * USER_SLOT_SPAN;

    if (code_base != 0) {
        *code_base = code;
    }
    if (stack_base != 0) {
        *stack_base = stack;
    }
}

static int resume_user_program_internal(Process* parent, Process* process, int print_banner);
static int resume_user_thread_internal(Process* parent,
                                       Process* process,
                                       Thread* thread,
                                       int print_banner);

static int continue_ready_threads(ThreadIdentity exclude) {
    Thread* next_ready =
        scheduler_claim_ready_thread(exclude, 0, 0, 0);
    if (next_ready == 0 || next_ready->owner == 0) {
        return 0;
    }
    Process* process = next_ready->owner;
    Process* parent = process->parent_pid != 0
        ? find_process_by_pid(process->parent_pid)
        : 0;
    return resume_user_thread_internal(parent, process, next_ready, 0);
}

static int parent_should_resume_immediately(const Process* parent) {
    if (parent == 0 || !parent->active) {
        return 0;
    }
    if (parent->wait_pending && parent->wait_reason != PROCESS_WAIT_CHILD) {
        return 0;
    }
    if ((parent->pause_reason == PROCESS_PAUSE_SLEEP ||
         parent->pause_reason == PROCESS_PAUSE_WAIT) &&
        parent->scheduler_state == SCHED_STATE_WAITING) {
        return 0;
    }
    return 1;
}

static int nested_syscall_waiter_active(const Process* completed) {
    Process* caller = current_process();
    return caller != 0 && caller != completed && caller->active;
}

int continue_ready_processes(uint32_t exclude_pid) {
    ThreadIdentity none = {0, 0};
    Thread* thread =
        scheduler_claim_ready_thread(none, exclude_pid, 0, 0);
    if (thread == 0 || thread->owner == 0) {
        return 0;
    }
    Process* process = thread->owner;
    Process* parent = process->parent_pid != 0
        ? find_process_by_pid(process->parent_pid) : 0;
    return resume_user_thread_internal(parent, process, thread, 0);
}

int continue_woken_processes(uint32_t exclude_pid) {
    ThreadIdentity none = {0, 0};
    Thread* thread =
        scheduler_claim_ready_thread(none, exclude_pid, 0, 1);
    if (thread == 0 || thread->owner == 0) {
        return 0;
    }
    Process* process = thread->owner;
    Process* parent = process->parent_pid != 0
        ? find_process_by_pid(process->parent_pid) : 0;
    return resume_user_thread_internal(parent, process, thread, 0);
}

int continue_background_processes(uint32_t exclude_pid) {
    ThreadIdentity none = {0, 0};
    Thread* thread =
        scheduler_claim_ready_thread(none, exclude_pid, 1, 0);
    if (thread == 0 || thread->owner == 0) {
        return 0;
    }
    Process* process = thread->owner;
    Process* parent = process->parent_pid != 0
        ? find_process_by_pid(process->parent_pid) : 0;
    return resume_user_thread_internal(parent, process, thread, 0);
}

static int wait_for_terminal_process(Process* process) {
    if (process == 0) {
        return 0;
    }
    uint32_t pid = process->pid;
    uint32_t generation = process->generation;
    while (process->pid == pid && process->generation == generation &&
           process->state != PROCESS_STATE_RETURNED &&
           process->state != PROCESS_STATE_FAILED) {
        if (continue_ready_processes(0)) {
            continue;
        }
        __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
    // Let preempted reply producers reach their next wait boundary before the
    // shell publishes a new prompt. The pass is bounded so a CPU-bound
    // background process cannot indefinitely withhold console ownership.
    for (uint32_t i = 0; i < USER_PROGRAM_SLOT_COUNT; i++) {
        if (!continue_ready_processes(pid)) {
            break;
        }
    }
    return 1;
}

static void user_input_reset() {
    char discarded = 0;
    while (keyboard.try_read_char(&discarded)) {
    }
}
