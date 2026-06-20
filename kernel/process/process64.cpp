#include "fs/vfs.h"
#include "kernel/kutil64.h"
#include "kernel/process64.h"

uint32_t user_program_depth = 0;
uint32_t next_pid = 1;
Process process_table[PROCESS_TABLE_SIZE];
Process* process_stack[USER_PROGRAM_SLOT_COUNT];
Process* sched_queue[SCHED_QUEUE_SIZE];
uint32_t sched_queue_count = 0;
uint32_t sched_queue_head = 0;
uint32_t sched_last_pid = 0;
uint32_t sched_switch_count = 0;
uint32_t sched_yield_count = 0;

Process* current_process() {
    if (user_program_depth == 0) {
        return 0;
    }
    return process_stack[user_program_depth - 1];
}

void process_clear(Process* process) {
    if (process == 0) {
        return;
    }

    process->pid = 0;
    process->parent_pid = 0;
    process->name[0] = '\0';
    process->code_base = 0;
    process->stack_base = 0;
    process->heap_base = 0;
    process->heap_break = 0;
    process->heap_mapped_end = 0;
    process->heap_limit = 0;
    process->entry_point = 0;
    process->image_size = 0;
    process->code_page_count = 0;
    process->stack_page_count = 0;
    process->heap_page_count = 0;
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
    process->cwd[0] = '/';
    process->cwd[1] = '\0';
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

const char* process_get_cwd(const Process* process) {
    if (process == 0 || process->cwd[0] == '\0') {
        return "/";
    }
    return process->cwd;
}

void process_copy_cwd(Process* process, const char* cwd) {
    if (process == 0) {
        return;
    }

    copy_string64(process->cwd, sizeof(process->cwd), (cwd != 0 && cwd[0] != '\0') ? cwd : "/");
    if (process->cwd[0] == '\0') {
        process->cwd[0] = '/';
        process->cwd[1] = '\0';
    }
}

static int process_is_waitable_result(const Process* process) {
    if (process == 0 || process->pid == 0 || process->active || process->reaped) {
        return 0;
    }
    return process->state == PROCESS_STATE_RETURNED || process->state == PROCESS_STATE_FAILED;
}

static uint32_t reap_old_child_results(uint32_t parent_pid, uint32_t keep_count) {
    uint32_t reaped_count = 0;
    for (;;) {
        uint32_t result_count = 0;
        Process* oldest = 0;
        for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
            Process* process = &process_table[i];
            if (process->parent_pid != parent_pid || !process_is_waitable_result(process)) {
                continue;
            }
            result_count++;
            if (oldest == 0 || process->pid < oldest->pid) {
                oldest = process;
            }
        }

        if (result_count <= keep_count || oldest == 0) {
            return reaped_count;
        }

        oldest->reaped = 1;
        reaped_count++;
    }
}

void process_mark_failed(Process* process, uint32_t reason, uint32_t status_code) {
    if (process == 0) {
        return;
    }

    vfs_close_all_for_owner(process->pid);
    process->state = PROCESS_STATE_FAILED;
    process->termination_reason = reason;
    process->status_code = status_code;
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->wake_tick = 0;
    process->resumable = 0;
    process->active = 0;
    process->reaped = 0;
    reap_old_child_results(process->parent_pid, PROCESS_CHILD_RESULT_HISTORY_LIMIT);
}

void process_mark_returned(Process* process, uint32_t reason, uint32_t status_code) {
    if (process == 0) {
        return;
    }

    vfs_close_all_for_owner(process->pid);
    process->state = PROCESS_STATE_RETURNED;
    process->termination_reason = reason;
    process->status_code = status_code;
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->wake_tick = 0;
    process->resumable = 0;
    process->active = 0;
    process->reaped = 0;
    reap_old_child_results(process->parent_pid, PROCESS_CHILD_RESULT_HISTORY_LIMIT);
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

void scheduler_enqueue(Process* process) {
    if (process == 0 || scheduler_queue_contains(process) || sched_queue_count >= SCHED_QUEUE_SIZE) {
        return;
    }

    uint32_t index = (sched_queue_head + sched_queue_count) % SCHED_QUEUE_SIZE;
    sched_queue[index] = process;
    sched_queue_count++;
    process->scheduler_state = SCHED_STATE_READY;
    process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
}

void scheduler_remove(Process* process) {
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

void scheduler_mark_running(Process* process) {
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

void scheduler_mark_waiting(Process* process) {
    if (process == 0) {
        return;
    }

    process->scheduler_state = SCHED_STATE_WAITING;
}

void scheduler_mark_sleeping(Process* process, uint32_t wake_tick) {
    if (process == 0) {
        return;
    }

    scheduler_remove(process);
    process->scheduler_state = SCHED_STATE_WAITING;
    process->wake_tick = wake_tick;
}

void scheduler_mark_finished(Process* process) {
    if (process == 0) {
        return;
    }

    scheduler_remove(process);
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->timeslice_ticks = 0;
}

void scheduler_yield_current() {
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

void scheduler_on_tick() {
    Process* process = current_process();
    if (process == 0) {
        return;
    }

    process->runtime_ticks++;
    if (process->timeslice_ticks > 0) {
        process->timeslice_ticks--;
    }
}

void scheduler_wake_sleeping_processes(uint32_t tick_now) {
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

Process* find_next_ready_process(uint32_t exclude_pid) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Process* process = sched_queue[index];
        if (process == 0 || process->pid == 0) {
            continue;
        }
        if (process->pid == exclude_pid) {
            continue;
        }
        if (!process->active || !process->resumable) {
            continue;
        }
        if (process->scheduler_state != SCHED_STATE_READY) {
            continue;
        }
        return process;
    }
    return 0;
}

Process* find_next_background_ready_process(uint32_t exclude_pid) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Process* process = sched_queue[index];
        if (process == 0 || process->pid == 0) {
            continue;
        }
        if (process->pid == exclude_pid) {
            continue;
        }
        if (!process->active || !process->resumable || !process->background) {
            continue;
        }
        if (process->scheduler_state != SCHED_STATE_READY) {
            continue;
        }
        return process;
    }
    return 0;
}

Process* find_next_woken_process(uint32_t exclude_pid) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Process* process = sched_queue[index];
        if (process == 0 || process->pid == 0) {
            continue;
        }
        if (process->pid == exclude_pid) {
            continue;
        }
        if (!process->active || !process->resumable) {
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

int scheduler_should_preempt_current() {
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

int process_record_is_active(const Process* process) {
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

Process* allocate_process_record() {
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

const Process* find_last_child_process(uint32_t parent_pid) {
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

Process* find_waitable_child_process(uint32_t parent_pid) {
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

uint32_t reap_all_child_processes(uint32_t parent_pid) {
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

uint32_t count_unfinished_child_processes(uint32_t parent_pid) {
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

Process* find_last_paused_child_process(uint32_t parent_pid) {
    Process* latest = 0;
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 || process->parent_pid != parent_pid) {
            continue;
        }
        if (!process->active || !process->resumable) {
            continue;
        }
        if (process->state != PROCESS_STATE_PAUSED) {
            continue;
        }
        if (latest == 0 || process->pid > latest->pid) {
            latest = process;
        }
    }
    return latest;
}

Process* find_process_by_pid(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return 0;
}
