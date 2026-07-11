#include "fs/vfs.h"
#include "kernel/handle/kernel_handle.h"
#include "kernel/kutil64.h"
#include "kernel/process64.h"
#include "kernel/service/service_registry.h"

uint32_t user_program_depth = 0;
uint32_t next_pid = 1;
uint32_t next_process_generation = 1;
Process process_table[PROCESS_TABLE_SIZE];
Process* process_stack[USER_PROGRAM_SLOT_COUNT];
Process* sched_queue[SCHED_QUEUE_SIZE];
uint32_t sched_queue_count = 0;
uint32_t sched_queue_head = 0;
uint32_t sched_last_pid = 0;
uint32_t sched_switch_count = 0;
uint32_t sched_yield_count = 0;
uint32_t input_focus_pid = 0;

const char* process_wait_reason_name(uint32_t reason) {
    if (reason == PROCESS_WAIT_TIMER) {
        return "timer";
    }
    if (reason == PROCESS_WAIT_CHILD) {
        return "child";
    }
    if (reason == PROCESS_WAIT_IPC) {
        return "ipc";
    }
    if (reason == PROCESS_WAIT_INPUT) {
        return "input";
    }
    if (reason == PROCESS_WAIT_KEY) {
        return "key";
    }
    if (reason == PROCESS_WAIT_CHAR) {
        return "char";
    }
    return "none";
}

void process_wait_reset(Process* process) {
    if (process == 0) {
        return;
    }
    process->wait_pending = 0;
    process->wait_has_deadline = 0;
    process->wait_reserved[0] = 0;
    process->wait_reserved[1] = 0;
    process->wait_reserved[2] = 0;
    process->wait_reason = PROCESS_WAIT_NONE;
    process->wait_result = PROCESS_WAIT_OK;
    process->wait_deadline = 0;
    process->wait_user_address = 0;
    process->wake_tick = 0;
}

int process_wait_begin(Process* process,
                       uint32_t reason,
                       uint64_t user_address,
                       uint32_t timeout_ticks,
                       uint32_t tick_now) {
    if (process == 0 || reason == PROCESS_WAIT_NONE || process->wait_pending) {
        return 0;
    }

    scheduler_remove(process);
    process->wait_pending = 1;
    process->wait_reason = reason;
    process->wait_result = PROCESS_WAIT_OK;
    process->wait_user_address = user_address;
    process->wait_has_deadline = timeout_ticks != 0 ? 1 : 0;
    process->wait_deadline = timeout_ticks != 0 ? tick_now + timeout_ticks : 0;
    process->wake_tick = process->wait_deadline;
    process->scheduler_state = SCHED_STATE_WAITING;
    return 1;
}

int process_wait_signal(Process* process, uint32_t reason, int32_t result) {
    if (process == 0 || !process->wait_pending || process->wait_reason != reason) {
        return 0;
    }

    process->wait_pending = 0;
    process->wait_has_deadline = 0;
    process->wait_result = result;
    process->wait_deadline = 0;
    process->wake_tick = 0;
    if (process->active && process->state == PROCESS_STATE_PAUSED && process->resumable) {
        process->scheduler_state = SCHED_STATE_READY;
        process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
        scheduler_enqueue(process);
    }
    return 1;
}

int process_wait_cancel(Process* process, uint32_t reason, int32_t result) {
    return process_wait_signal(process, reason, result);
}

void process_wait_tick(uint32_t tick_now) {
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (!process->active || !process->wait_pending || !process->wait_has_deadline) {
            continue;
        }
        if ((int32_t)(tick_now - process->wait_deadline) < 0) {
            continue;
        }

        int32_t result = process->wait_reason == PROCESS_WAIT_TIMER
            ? PROCESS_WAIT_OK
            : PROCESS_WAIT_TIMEOUT;
        process_wait_signal(process, process->wait_reason, result);
    }
}

int process_wait_is_pending(const Process* process) {
    return process != 0 && process->wait_pending != 0;
}

Process* current_process() {
    if (user_program_depth == 0) {
        return 0;
    }
    return process_stack[user_program_depth - 1];
}

static uint32_t process_next_generation() {
    uint32_t generation = next_process_generation++;
    if (next_process_generation == 0) {
        next_process_generation = 1;
    }
    return generation == 0 ? process_next_generation() : generation;
}

static void process_reset_address_space_record(Process* process) {
    if (process == 0) {
        return;
    }

    uint64_t root_phys = process->address_space.root_phys;
    process->address_space.root_phys = root_phys;
    process->address_space.code_base = 0;
    process->address_space.elf_link_base = 0;
    process->address_space.stack_guard_base = 0;
    process->address_space.stack_base = 0;
    process->address_space.heap_base = 0;
    process->address_space.heap_break = 0;
    process->address_space.heap_mapped_end = 0;
    process->address_space.heap_limit = 0;
    process->address_space.code_page_count = 0;
    process->address_space.elf_alias_page_count = 0;
    process->address_space.stack_guard_page_count = 0;
    process->address_space.stack_page_count = 0;
    process->address_space.heap_page_count = 0;
    process->address_space.region_count = 0;
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        process->address_space.regions[i].active = 0;
        process->address_space.regions[i].reserved0 = 0;
        process->address_space.regions[i].reserved1 = 0;
        process->address_space.regions[i].rights = 0;
        process->address_space.regions[i].start = 0;
        process->address_space.regions[i].end = 0;
    }
}

ProcessIdentity process_identity(const Process* process) {
    ProcessIdentity identity;
    identity.pid = process != 0 ? process->pid : 0;
    identity.generation = process != 0 ? process->generation : 0;
    return identity;
}

int process_identity_matches(const Process* process, ProcessIdentity identity) {
    if (process == 0 || identity.pid == 0 || process->pid != identity.pid) {
        return 0;
    }
    return identity.generation == 0 || process->generation == identity.generation;
}

void process_assign_identity(Process* process, uint32_t pid, const Process* parent) {
    if (process == 0) {
        return;
    }
    process->pid = pid;
    process->generation = process_next_generation();
    process->parent_pid = parent != 0 ? parent->pid : 0;
    process->parent_generation = parent != 0 ? parent->generation : 0;
}

void process_event_queue_reset(Process* process) {
    if (process == 0) {
        return;
    }
    input_event_queue_init(&process->event_queue);
}

void process_ipc_mailbox_reset(Process* process) {
    if (process == 0) {
        return;
    }
    ipc_mailbox_init(&process->ipc_mailbox);
}

int process_event_queue_push(Process* process, const OsInputEvent* event) {
    if (process == 0) {
        return 0;
    }
    return input_event_queue_push_drop_oldest(&process->event_queue, event);
}

int process_event_queue_pop(Process* process, OsInputEvent* event) {
    if (process == 0) {
        return 0;
    }
    return input_event_queue_pop(&process->event_queue, event);
}

uint32_t process_event_queue_count(const Process* process) {
    return process == 0 ? 0 : input_event_queue_count(&process->event_queue);
}

uint32_t process_event_queue_delivered_count(const Process* process) {
    return process == 0 ? 0 : input_event_queue_delivered_count(&process->event_queue);
}

uint32_t process_event_queue_dropped_count(const Process* process) {
    return process == 0 ? 0 : input_event_queue_dropped_count(&process->event_queue);
}

int process_ipc_mailbox_push(Process* process, const OsIpcMessage* message) {
    if (process == 0) {
        return 0;
    }
    return ipc_mailbox_push(&process->ipc_mailbox, message);
}

int process_ipc_mailbox_pop(Process* process, OsIpcMessage* message) {
    if (process == 0) {
        return 0;
    }
    return ipc_mailbox_pop(&process->ipc_mailbox, message);
}

int process_ipc_mailbox_push_v2(Process* process, const OsIpcMessageV2* message) {
    if (process == 0) {
        return 0;
    }
    return ipc_mailbox_push_v2(&process->ipc_mailbox, message);
}

int process_ipc_mailbox_pop_v2(Process* process, OsIpcMessageV2* message) {
    if (process == 0) {
        return 0;
    }
    return ipc_mailbox_pop_v2(&process->ipc_mailbox, message);
}

uint32_t process_ipc_mailbox_count(const Process* process) {
    return process == 0 ? 0 : ipc_mailbox_count(&process->ipc_mailbox);
}

uint32_t process_ipc_mailbox_delivered_count(const Process* process) {
    return process == 0 ? 0 : ipc_mailbox_delivered_count(&process->ipc_mailbox);
}

uint32_t process_ipc_mailbox_dropped_count(const Process* process) {
    return process == 0 ? 0 : ipc_mailbox_dropped_count(&process->ipc_mailbox);
}

void process_ipc_wait_begin(Process* process) {
    process_wait_begin(process, PROCESS_WAIT_IPC, 0, 0, 0);
}

void process_ipc_wait_end(Process* process) {
    if (process != 0 && process->wait_reason == PROCESS_WAIT_IPC) {
        process_wait_reset(process);
    }
}

int process_ipc_waiting(const Process* process) {
    return process != 0 &&
           process->wait_pending &&
           process->wait_reason == PROCESS_WAIT_IPC;
}

void process_input_wait_begin(Process* process) {
    process_wait_begin(process, PROCESS_WAIT_INPUT, 0, 0, 0);
}

void process_input_wait_end(Process* process) {
    if (process != 0 &&
        (process->wait_reason == PROCESS_WAIT_INPUT ||
         process->wait_reason == PROCESS_WAIT_KEY ||
         process->wait_reason == PROCESS_WAIT_CHAR)) {
        process_wait_reset(process);
    }
}

int process_input_waiting(const Process* process) {
    return process != 0 &&
           process->wait_pending &&
           (process->wait_reason == PROCESS_WAIT_INPUT ||
            process->wait_reason == PROCESS_WAIT_KEY ||
            process->wait_reason == PROCESS_WAIT_CHAR);
}

static int process_can_receive_focus(const Process* process) {
    if (process == 0 || process->pid == 0 || !process->active) {
        return 0;
    }
    if (process->state == PROCESS_STATE_RETURNED || process->state == PROCESS_STATE_FAILED ||
        process->state == PROCESS_STATE_EMPTY) {
        return 0;
    }
    return 1;
}

uint32_t process_focused_pid() {
    Process* process = find_process_by_pid(input_focus_pid);
    if (!process_can_receive_focus(process)) {
        process_wait_cancel(process, PROCESS_WAIT_INPUT, PROCESS_WAIT_CANCELLED);
        process_wait_cancel(process, PROCESS_WAIT_KEY, PROCESS_WAIT_CANCELLED);
        process_wait_cancel(process, PROCESS_WAIT_CHAR, PROCESS_WAIT_CANCELLED);
        input_focus_pid = 0;
    }
    return input_focus_pid;
}

Process* process_focused() {
    uint32_t pid = process_focused_pid();
    return pid == 0 ? 0 : find_process_by_pid(pid);
}

int process_set_focus(uint32_t pid) {
    Process* process = find_process_by_pid(pid);
    if (!process_can_receive_focus(process)) {
        return 0;
    }
    Process* old_focus = process_focused();
    if (old_focus != 0 && old_focus->pid != pid) {
        process_wait_cancel(old_focus, PROCESS_WAIT_INPUT, PROCESS_WAIT_CANCELLED);
        process_wait_cancel(old_focus, PROCESS_WAIT_KEY, PROCESS_WAIT_CANCELLED);
        process_wait_cancel(old_focus, PROCESS_WAIT_CHAR, PROCESS_WAIT_CANCELLED);
    }
    input_focus_pid = pid;
    return 1;
}

void process_clear_focus(uint32_t pid) {
    Process* old_focus = process_focused();
    if (pid == 0 || input_focus_pid == pid) {
        input_focus_pid = 0;
    }
    if (old_focus != 0 && (pid == 0 || old_focus->pid == pid)) {
        process_wait_cancel(old_focus, PROCESS_WAIT_INPUT, PROCESS_WAIT_CANCELLED);
        process_wait_cancel(old_focus, PROCESS_WAIT_KEY, PROCESS_WAIT_CANCELLED);
        process_wait_cancel(old_focus, PROCESS_WAIT_CHAR, PROCESS_WAIT_CANCELLED);
    }
}

void process_clear(Process* process) {
    if (process == 0) {
        return;
    }

    service_unregister_owner(process->pid);
    process_clear_focus(process->pid);
    process->pid = 0;
    process->generation = 0;
    process->parent_pid = 0;
    process->parent_generation = 0;
    process->name[0] = '\0';
    process->code_base = 0;
    process->elf_link_base = 0;
    process->stack_guard_base = 0;
    process->stack_base = 0;
    process->heap_base = 0;
    process->heap_break = 0;
    process->heap_mapped_end = 0;
    process->heap_limit = 0;
    process->entry_point = 0;
    process->image_size = 0;
    process->code_page_count = 0;
    process->elf_alias_page_count = 0;
    process->stack_guard_page_count = 0;
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
    process_wait_reset(process);
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
    process_reset_address_space_record(process);
    kernel_handle_table_init(&process->handle_table);
    process_event_queue_reset(process);
    process_ipc_mailbox_reset(process);
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

static int process_child_matches_parent(const Process* process,
                                        uint32_t parent_pid,
                                        uint32_t parent_generation) {
    if (process == 0 || process->parent_pid != parent_pid) {
        return 0;
    }
    return parent_generation == 0 || process->parent_generation == parent_generation;
}

static uint32_t current_generation_for_pid(uint32_t pid) {
    Process* parent = find_process_by_pid(pid);
    return parent != 0 ? parent->generation : 0;
}

static int process_is_waitable_result(const Process* process) {
    if (process == 0 || process->pid == 0 || process->active || process->reaped) {
        return 0;
    }
    return process->state == PROCESS_STATE_RETURNED || process->state == PROCESS_STATE_FAILED;
}

static uint32_t reap_old_child_results(uint32_t parent_pid,
                                       uint32_t parent_generation,
                                       uint32_t keep_count) {
    uint32_t reaped_count = 0;
    for (;;) {
        uint32_t result_count = 0;
        Process* oldest = 0;
        for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
            Process* process = &process_table[i];
            if (!process_child_matches_parent(process, parent_pid, parent_generation) ||
                !process_is_waitable_result(process)) {
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

static void process_cleanup_owned_children(uint32_t parent_pid, uint32_t parent_generation) {
    if (parent_pid == 0) {
        return;
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* child = &process_table[i];
        if (child->pid == 0 ||
            !process_child_matches_parent(child, parent_pid, parent_generation)) {
            continue;
        }
        if (child->active) {
            child->parent_pid = 0;
            child->parent_generation = 0;
            continue;
        }
        if (process_is_waitable_result(child)) {
            child->reaped = 1;
        }
    }
}

static void process_finish(Process* process,
                           uint32_t final_state,
                           uint32_t reason,
                           uint32_t status_code) {
    if (process == 0) {
        return;
    }

    uint32_t parent_pid = process->parent_pid;
    uint32_t parent_generation = process->parent_generation;
    uint32_t own_pid = process->pid;
    uint32_t own_generation = process->generation;

    scheduler_remove(process);
    vfs_close_all_for_owner(process->pid);
    kernel_handle_table_init(&process->handle_table);
    service_unregister_owner(process->pid);
    process->state = final_state;
    process->termination_reason = reason;
    process->status_code = status_code;
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->resumable = 0;
    process->active = 0;
    process->reaped = 0;
    process_wait_reset(process);
    process_clear_focus(process->pid);
    process_event_queue_reset(process);
    process_ipc_mailbox_reset(process);
    process_cleanup_owned_children(own_pid, own_generation);
    reap_old_child_results(parent_pid,
                           parent_generation,
                           PROCESS_CHILD_RESULT_HISTORY_LIMIT);

    ProcessIdentity parent_identity;
    parent_identity.pid = parent_pid;
    parent_identity.generation = parent_generation;
    Process* parent = find_process_by_identity(parent_identity);
    process_wait_signal(parent, PROCESS_WAIT_CHILD, PROCESS_WAIT_OK);
}

void process_mark_failed(Process* process, uint32_t reason, uint32_t status_code) {
    process_finish(process, PROCESS_STATE_FAILED, reason, status_code);
}

void process_mark_returned(Process* process, uint32_t reason, uint32_t status_code) {
    process_finish(process, PROCESS_STATE_RETURNED, reason, status_code);
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
    if (process == 0) {
        return;
    }
    if (scheduler_queue_contains(process)) {
        process->scheduler_state = SCHED_STATE_READY;
        process->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
        return;
    }
    if (sched_queue_count >= SCHED_QUEUE_SIZE) {
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

    if (process->wait_reason == PROCESS_WAIT_CHILD) {
        process_wait_reset(process);
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
    process_wait_tick(tick_now);
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
    uint32_t parent_generation = current_generation_for_pid(parent_pid);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        if (process->pid == 0 ||
            !process_child_matches_parent(process, parent_pid, parent_generation)) {
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
    uint32_t parent_generation = current_generation_for_pid(parent_pid);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 ||
            !process_child_matches_parent(process, parent_pid, parent_generation)) {
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
    uint32_t parent_generation = current_generation_for_pid(parent_pid);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 ||
            !process_child_matches_parent(process, parent_pid, parent_generation)) {
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
    uint32_t parent_generation = current_generation_for_pid(parent_pid);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        if (process->pid == 0 ||
            !process_child_matches_parent(process, parent_pid, parent_generation)) {
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
    uint32_t parent_generation = current_generation_for_pid(parent_pid);
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        Process* process = &process_table[i];
        if (process->pid == 0 ||
            !process_child_matches_parent(process, parent_pid, parent_generation)) {
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
    if (pid == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return 0;
}

Process* find_process_by_identity(ProcessIdentity identity) {
    if (identity.pid == 0 || identity.generation == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_identity_matches(&process_table[i], identity)) {
            return &process_table[i];
        }
    }
    return 0;
}

Process* find_process_by_identity_compat(uint32_t pid, uint32_t generation) {
    ProcessIdentity identity;
    identity.pid = pid;
    identity.generation = generation;
    return generation == 0 ? find_process_by_pid(pid) : find_process_by_identity(identity);
}
