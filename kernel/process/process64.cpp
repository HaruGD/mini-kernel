#include "fs/vfs.h"
#include "kernel/handle/kernel_handle.h"
#include "kernel/handle/kernel_objects.h"
#include "kernel/fault_injection.h"
#include "kernel/kutil64.h"
#include "kernel/process64.h"
#include "kernel/process_surface.h"
#include "kernel/service/service_registry.h"
#include "kernel/spinlock.h"
#include "kernel/syscall64.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vm.h"

uint32_t user_program_depth = 0;
uint32_t next_pid = 1;
uint32_t next_process_generation = 1;
uint32_t next_tid = 1;
uint32_t next_thread_generation = 1;
uint64_t next_wait_sequence = 1;
Process process_table[PROCESS_TABLE_SIZE];
Thread thread_table[THREAD_TABLE_SIZE];
Process* process_stack[EXECUTION_STACK_SIZE];
Thread* thread_stack[EXECUTION_STACK_SIZE];
Thread* sched_queue[SCHED_QUEUE_SIZE];
uint32_t sched_queue_count = 0;
uint32_t sched_queue_head = 0;
uint32_t sched_last_pid = 0;
uint32_t sched_switch_count = 0;
uint32_t sched_yield_count = 0;
uint32_t input_focus_pid = 0;
static KernelSpinlock process_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_PROCESS, "process_scheduler");

extern "C" void display_session_process_cleanup(uint32_t pid,
                                                uint32_t generation)
    __attribute__((weak));

static void recover_display_session_for_process(uint32_t pid,
                                                uint32_t generation) {
    if (display_session_process_cleanup != 0) {
        display_session_process_cleanup(pid, generation);
    }
}

static void scheduler_enqueue_unlocked(Thread* thread);
static void scheduler_remove_unlocked(Thread* thread);
static void thread_wait_reset_unlocked(Thread* thread);
static void thread_join_release_claim_unlocked(Thread* waiter);

static uint32_t thread_quantum(const Thread* thread) {
    if (thread != 0 && thread->priority == OS_THREAD_PRIORITY_LOW) {
        return 4;
    }
    if (thread != 0 && thread->priority == OS_THREAD_PRIORITY_HIGH) {
        return 8;
    }
    return SCHED_DEFAULT_TIMESLICE;
}

extern "C" void kernel_sync_thread_exit(Thread* thread) __attribute__((weak));
extern "C" void kernel_sync_timeout_thread(Thread* thread) __attribute__((weak));

void thread_context_reset(ThreadContext* context) {
    if (context == 0) {
        return;
    }
    uint8_t* bytes = (uint8_t*)context;
    for (uint32_t i = 0; i < sizeof(ThreadContext); i++) {
        bytes[i] = 0;
    }
    context->timeslice_ticks = SCHED_DEFAULT_TIMESLICE;
    context->wait_result = PROCESS_WAIT_OK;
}

static void thread_record_zero(Thread* thread) {
    if (thread == 0) {
        return;
    }
    uint8_t* bytes = (uint8_t*)thread;
    for (uint32_t i = 0; i < sizeof(Thread); i++) {
        bytes[i] = 0;
    }
    thread->context = &thread->context_storage;
    thread_context_reset(thread->context);
    thread->priority = OS_THREAD_PRIORITY_NORMAL;
}

void process_system_init() {
    user_program_depth = 0;
    next_pid = 1;
    next_process_generation = 1;
    next_tid = 1;
    next_thread_generation = 1;
    next_wait_sequence = 1;
    sched_queue_count = 0;
    sched_queue_head = 0;
    sched_last_pid = 0;
    sched_switch_count = 0;
    sched_yield_count = 0;
    input_focus_pid = 0;
    for (uint32_t i = 0; i < EXECUTION_STACK_SIZE; i++) {
        process_stack[i] = 0;
        thread_stack[i] = 0;
    }
    for (uint32_t i = 0; i < SCHED_QUEUE_SIZE; i++) {
        sched_queue[i] = 0;
    }
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        thread_record_zero(&thread_table[i]);
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        process_clear(&process_table[i]);
    }
}

static void snapshot_copy_name(char* destination, const char* source) {
    uint32_t i = 0;
    while (i + 1 < PROCESS_NAME_MAX && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

void process_get_diagnostic_snapshot(SchedulerDiagnosticSnapshot* snapshot) {
    if (snapshot == 0) {
        return;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        snapshot->process_count = 0;
        snapshot->queue_count = 0;
        return;
    }
    snapshot->process_count = PROCESS_TABLE_SIZE;
    snapshot->queue_count = sched_queue_count;
    snapshot->queue_head = sched_queue_head;
    snapshot->last_pid = sched_last_pid;
    snapshot->switch_count = sched_switch_count;
    snapshot->yield_count = sched_yield_count;
    snapshot->focused_pid = input_focus_pid;
    snapshot->thread_count = 0;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        if (thread_table[i].tid != 0) {
            snapshot->thread_count++;
        }
    }
    for (uint32_t i = 0; i < SCHED_QUEUE_SIZE; i++) {
        Thread* queued = sched_queue[i];
        snapshot->queue_pids[i] = queued != 0 ? queued->owner_pid : 0;
        snapshot->queue_tids[i] = queued != 0 ? queued->tid : 0;
        snapshot->queue_thread_generations[i] = queued != 0 ? queued->generation : 0;
        snapshot->queue_scheduler_states[i] =
            queued != 0 ? queued->context->scheduler_state : SCHED_STATE_NONE;
        snapshot->queue_runtime_ticks[i] =
            queued != 0 ? queued->context->runtime_ticks : 0;
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        const Process* process = &process_table[i];
        ProcessDiagnosticSnapshot* out = &snapshot->processes[i];
        out->pid = process->pid;
        out->generation = process->generation;
        out->parent_pid = process->parent_pid;
        out->parent_generation = process->parent_generation;
        snapshot_copy_name(out->name, process->name);
        out->slot_index = process->slot_index;
        out->state = process->state;
        out->termination_reason = process->termination_reason;
        out->status_code = process->status_code;
        out->scheduler_state = process->scheduler_state;
        out->pause_reason = process->pause_reason;
        out->wait_reason = process->wait_reason;
        out->permissions = process->permissions;
        out->runtime_ticks = process->runtime_ticks;
        out->timeslice_ticks = process->timeslice_ticks;
        out->wake_tick = process->wake_tick;
        out->wait_deadline = process->wait_deadline;
        out->mapping_count = process->address_space.region_count;
        out->active = process->active;
        out->reaped = process->reaped;
        out->resumable = process->resumable;
        out->background = process->background;
        out->wait_pending = process->wait_pending;
        out->wait_has_deadline = process->wait_has_deadline;
        out->handle_count = kernel_handle_count_type(&process->handle_table,
                                                     KERNEL_HANDLE_TYPE_NONE);
        out->thread_count = process->thread_count;
        out->main_tid = process->main_thread_identity.tid;
        out->main_thread_generation = process->main_thread_identity.generation;
        out->fault_thread_identity = process->fault_thread_identity;
        out->thread_runtime_ticks = 0;
        out->thread_preemption_count = 0;
        out->thread_yield_count = 0;
        out->thread_block_count = 0;
        out->thread_wake_count = 0;
        out->thread_switch_count = 0;
        for (uint32_t t = 0; t < THREAD_TABLE_SIZE; t++) {
            const Thread* thread = &thread_table[t];
            if (thread->owner != process || thread->context == 0) {
                continue;
            }
            out->thread_runtime_ticks += thread->context->runtime_ticks_total;
            out->thread_preemption_count += thread->context->preemption_count;
            out->thread_yield_count += thread->context->yield_count;
            out->thread_block_count += thread->context->block_count;
            out->thread_wake_count += thread->context->wake_count;
            out->thread_switch_count += thread->context->switch_count;
        }
        ipc_mailbox_get_stats(&process->ipc_mailbox, &out->mailbox);
    }
    kernel_spinlock_release(&process_lock, &token);
}

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
    if (reason == PROCESS_WAIT_THREAD_JOIN) {
        return "thread-join";
    }
    if (reason == PROCESS_WAIT_MUTEX) {
        return "mutex";
    }
    if (reason == PROCESS_WAIT_SEMAPHORE) {
        return "semaphore";
    }
    if (reason == PROCESS_WAIT_CONDITION) {
        return "condition";
    }
    return "none";
}

static uint32_t thread_next_id() {
    uint32_t tid = next_tid++;
    if (next_tid == 0) {
        next_tid = 1;
    }
    return tid == 0 ? thread_next_id() : tid;
}

static uint32_t thread_next_generation() {
    uint32_t generation = next_thread_generation++;
    if (next_thread_generation == 0) {
        next_thread_generation = 1;
    }
    return generation == 0 ? thread_next_generation() : generation;
}

ThreadIdentity thread_identity(const Thread* thread) {
    ThreadIdentity identity;
    identity.tid = thread != 0 ? thread->tid : 0;
    identity.generation = thread != 0 ? thread->generation : 0;
    return identity;
}

int thread_identity_matches(const Thread* thread, ThreadIdentity identity) {
    return thread != 0 && identity.tid != 0 && identity.generation != 0 &&
           thread->tid == identity.tid && thread->generation == identity.generation;
}

Thread* find_thread_by_identity(ThreadIdentity identity) {
    if (identity.tid == 0 || identity.generation == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        if (thread_identity_matches(&thread_table[i], identity)) {
            Thread* result = &thread_table[i];
            kernel_spinlock_release(&process_lock, &token);
            return result;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return 0;
}

Thread* process_main_thread(const Process* process) {
    if (process == 0 || process->main_thread_identity.tid == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread_identity_matches(thread, process->main_thread_identity) &&
            thread->owner == process && thread->is_main) {
            return thread;
        }
    }
    return 0;
}

Thread* current_thread() {
    if (user_program_depth == 0 || user_program_depth > EXECUTION_STACK_SIZE) {
        return 0;
    }
    return thread_stack[user_program_depth - 1];
}

Process* current_process() {
    Thread* thread = current_thread();
    if (thread != 0) {
        return thread->owner;
    }
    if (user_program_depth == 0 || user_program_depth > EXECUTION_STACK_SIZE) {
        return 0;
    }
    return process_stack[user_program_depth - 1];
}

Thread* allocate_thread_record(Process* owner, int is_main) {
    if (owner == 0 || owner->pid == 0 || owner->generation == 0 ||
        owner->thread_count >= THREADS_PER_PROCESS_MAX || owner->exiting) {
        return 0;
    }
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_THREAD_RECORD)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    Thread* result = 0;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        if (thread_table[i].tid == 0) {
            result = &thread_table[i];
            break;
        }
    }
    if (result == 0) {
        kernel_spinlock_release(&process_lock, &token);
        return 0;
    }
    thread_record_zero(result);
    result->tid = thread_next_id();
    result->generation = thread_next_generation();
    result->owner_pid = owner->pid;
    result->owner_generation = owner->generation;
    result->owner = owner;
    result->is_main = is_main != 0 ? 1 : 0;
    result->active = 1;
    if (result->is_main) {
        result->context = &owner->main_thread_context;
        thread_context_reset(result->context);
        owner->main_thread_identity = thread_identity(result);
    }
    owner->thread_count++;
    kernel_spinlock_release(&process_lock, &token);

    uint64_t kernel_stack =
        kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_THREAD_KERNEL_STACK)
            ? 0
            : (uint64_t)(uintptr_t)pmm_alloc_block();
    if (kernel_stack == 0) {
        KernelSpinlockToken rollback_token;
        if (kernel_spinlock_acquire(&process_lock, &rollback_token)) {
            if (owner->thread_count > 0) {
                owner->thread_count--;
            }
            if (result->is_main) {
                owner->main_thread_identity.tid = 0;
                owner->main_thread_identity.generation = 0;
            }
            thread_record_zero(result);
            kernel_spinlock_release(&process_lock, &rollback_token);
        }
        return 0;
    }
    for (uint64_t i = 0; i < VM_PAGE_SIZE; i++) {
        *((volatile uint8_t*)(uintptr_t)(kernel_stack + i)) = 0;
    }
    result->context->kernel_stack_base = kernel_stack;
    result->context->kernel_stack_page_count = 1;
    return result;
}

static Thread* wait_thread_for_process(Process* process) {
    Thread* selected = current_thread();
    if (selected != 0 && selected->owner == process) {
        return selected;
    }
    return process_main_thread(process);
}

static void thread_wait_reset_unlocked(Thread* thread) {
    if (thread == 0 || thread->context == 0) {
        return;
    }
    ThreadContext* context = thread->context;
    context->wait_pending = 0;
    context->wait_has_deadline = 0;
    for (uint32_t i = 0; i < sizeof(context->wait_reserved); i++) {
        context->wait_reserved[i] = 0;
    }
    context->wait_reason = PROCESS_WAIT_NONE;
    context->wait_result = PROCESS_WAIT_OK;
    context->wait_deadline = 0;
    context->wait_sequence = 0;
    context->wait_user_address = 0;
    context->wait_object_id = 0;
    context->wait_aux_object_id = 0;
    context->wait_target_tid = 0;
    context->wait_target_generation = 0;
    context->wake_tick = 0;
}

static void process_wait_reset_unlocked(Process* process) {
    thread_wait_reset_unlocked(wait_thread_for_process(process));
}

void process_wait_reset(Process* process) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    process_wait_reset_unlocked(process);
    kernel_spinlock_release(&process_lock, &token);
}

void thread_wait_reset(Thread* thread) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    thread_wait_reset_unlocked(thread);
    kernel_spinlock_release(&process_lock, &token);
}

static uint64_t wait_next_sequence_unlocked() {
    uint64_t sequence = next_wait_sequence++;
    if (next_wait_sequence == 0) {
        next_wait_sequence = 1;
    }
    if (sequence == 0) {
        sequence = next_wait_sequence++;
    }
    return sequence;
}

static int thread_wait_begin_unlocked(Thread* thread,
                                      uint32_t reason,
                                      uint64_t user_address,
                                      uint32_t timeout_ticks,
                                      uint32_t tick_now) {
    if (thread == 0 || thread->context == 0 || thread->owner == 0 ||
        !thread->active || !thread->owner->active || reason == PROCESS_WAIT_NONE ||
        thread->context->wait_pending) {
        return 0;
    }
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_THREAD_WAIT)) {
        return 0;
    }
    ThreadContext* context = thread->context;
    scheduler_remove_unlocked(thread);
    context->wait_pending = 1;
    context->wait_reason = reason;
    context->wait_result = PROCESS_WAIT_OK;
    context->wait_sequence = wait_next_sequence_unlocked();
    context->wait_user_address = user_address;
    context->wait_has_deadline = timeout_ticks != 0 ? 1 : 0;
    context->wait_deadline = timeout_ticks != 0 ? tick_now + timeout_ticks : 0;
    context->wake_tick = context->wait_deadline;
    context->scheduler_state = SCHED_STATE_WAITING;
    context->block_count++;
    return 1;
}

int thread_wait_begin(Thread* thread,
                      uint32_t reason,
                      uint64_t user_address,
                      uint32_t timeout_ticks,
                      uint32_t tick_now) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    int result = thread_wait_begin_unlocked(thread,
                                            reason,
                                            user_address,
                                            timeout_ticks,
                                            tick_now);
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

int process_wait_begin(Process* process,
                       uint32_t reason,
                       uint64_t user_address,
                       uint32_t timeout_ticks,
                       uint32_t tick_now) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    int result = thread_wait_begin_unlocked(wait_thread_for_process(process),
                                            reason,
                                            user_address,
                                            timeout_ticks,
                                            tick_now);
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

static int thread_wait_signal_unlocked(Thread* thread, uint32_t reason, int32_t result) {
    if (thread == 0 || thread->context == 0 ||
        !thread->context->wait_pending || thread->context->wait_reason != reason) {
        return 0;
    }

    ThreadContext* context = thread->context;
    if (reason == PROCESS_WAIT_THREAD_JOIN && result != PROCESS_WAIT_OK) {
        thread_join_release_claim_unlocked(thread);
    }
    context->wait_pending = 0;
    context->wait_has_deadline = 0;
    context->wait_result = result;
    context->wait_deadline = 0;
    context->wake_tick = 0;
    context->wake_count++;
    if (thread->active && thread->owner != 0 && thread->owner->active &&
        context->resumable) {
        context->scheduler_state = SCHED_STATE_READY;
        context->timeslice_ticks = thread_quantum(thread);
        scheduler_enqueue_unlocked(thread);
    }
    return 1;
}

int thread_wait_signal(Thread* thread, uint32_t reason, int32_t result) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    int signaled = thread_wait_signal_unlocked(thread, reason, result);
    kernel_spinlock_release(&process_lock, &token);
    return signaled;
}

int thread_wait_is_pending(const Thread* thread) {
    return thread != 0 && thread->context != 0 &&
           thread->context->wait_pending != 0;
}

Thread* thread_wait_find_oldest(Process* process,
                                uint32_t reason,
                                uint64_t object_id) {
    Thread* selected = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; process != 0 && i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner != process || thread->context == 0 ||
            !thread->context->wait_pending ||
            thread->context->wait_reason != reason ||
            thread->context->wait_object_id != object_id) {
            continue;
        }
        if (selected == 0 ||
            thread->context->wait_sequence < selected->context->wait_sequence) {
            selected = thread;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return selected;
}

int thread_wait_set_objects(Thread* thread,
                            uint64_t object_id,
                            uint64_t aux_object_id) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    int result = thread != 0 && thread->context != 0 &&
                 thread->context->wait_pending;
    if (result) {
        thread->context->wait_object_id = object_id;
        thread->context->wait_aux_object_id = aux_object_id;
    }
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

int thread_wait_retarget(Thread* thread,
                         uint32_t old_reason,
                         uint32_t new_reason,
                         uint64_t object_id,
                         uint64_t aux_object_id,
                         int32_t resume_result) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    int result = thread != 0 && thread->context != 0 &&
                 thread->context->wait_pending &&
                 thread->context->wait_reason == old_reason;
    if (result) {
        thread->context->wait_reason = new_reason;
        thread->context->wait_object_id = object_id;
        thread->context->wait_aux_object_id = aux_object_id;
        thread->context->wait_result = resume_result;
        thread->context->wait_has_deadline = 0;
        thread->context->wait_deadline = 0;
        thread->context->wake_tick = 0;
        thread->context->wait_sequence = wait_next_sequence_unlocked();
    }
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

int thread_wait_cancel_object(Process* process,
                              uint64_t object_id,
                              int32_t result) {
    int cancelled = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; process != 0 && i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        ThreadContext* context = thread->context;
        if (thread->owner != process || context == 0 ||
            !context->wait_pending ||
            (context->wait_object_id != object_id &&
             context->wait_aux_object_id != object_id)) {
            continue;
        }
        if (thread_wait_signal_unlocked(thread, context->wait_reason, result)) {
            cancelled++;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return cancelled;
}

uint32_t process_wait_count(const Process* process, uint32_t reason) {
    uint32_t count = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; process != 0 && i < THREAD_TABLE_SIZE; i++) {
        const Thread* thread = &thread_table[i];
        if (thread->owner == process && thread->context != 0 &&
            thread->context->wait_pending &&
            (reason == PROCESS_WAIT_NONE || thread->context->wait_reason == reason)) {
            count++;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return count;
}

int process_wait_signal(Process* process, uint32_t reason, int32_t result) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    Thread* selected = 0;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner != process || thread->context == 0 ||
            !thread->context->wait_pending ||
            thread->context->wait_reason != reason) {
            continue;
        }
        if (selected == 0 ||
            thread->context->wait_sequence < selected->context->wait_sequence) {
            selected = thread;
        }
    }
    int result_value = thread_wait_signal_unlocked(selected, reason, result);
    kernel_spinlock_release(&process_lock, &token);
    return result_value;
}

int process_wait_cancel(Process* process, uint32_t reason, int32_t result) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    int cancelled = 0;
    for (uint32_t i = 0; process != 0 && i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner == process &&
            thread_wait_signal_unlocked(thread, reason, result)) {
            cancelled++;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return cancelled;
}

void process_notify_queued_ipc(Process* process) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    for (uint32_t i = 0; process != 0 && i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner == process && thread->active &&
            thread->context->resumable &&
            thread->context->wait_reason == PROCESS_WAIT_NONE) {
            scheduler_enqueue_unlocked(thread);
            break;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
}

void process_wait_tick(uint32_t tick_now) {
    Thread* sync_timeouts[THREAD_TABLE_SIZE];
    uint32_t sync_timeout_count = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        ThreadContext* context = thread->context;
        if (!thread->active || thread->owner == 0 || !thread->owner->active ||
            !context->wait_pending || !context->wait_has_deadline) {
            continue;
        }
        if ((int32_t)(tick_now - context->wait_deadline) < 0) {
            continue;
        }

        if (context->wait_reason == PROCESS_WAIT_MUTEX ||
            context->wait_reason == PROCESS_WAIT_SEMAPHORE ||
            context->wait_reason == PROCESS_WAIT_CONDITION) {
            context->wait_has_deadline = 0;
            context->wait_deadline = 0;
            context->wake_tick = 0;
            sync_timeouts[sync_timeout_count++] = thread;
            continue;
        }
        int32_t result = context->wait_reason == PROCESS_WAIT_TIMER
            ? PROCESS_WAIT_OK
            : PROCESS_WAIT_TIMEOUT;
        thread_wait_signal_unlocked(thread, context->wait_reason, result);
    }
    kernel_spinlock_release(&process_lock, &token);
    if (kernel_sync_timeout_thread != 0) {
        for (uint32_t i = 0; i < sync_timeout_count; i++) {
            kernel_sync_timeout_thread(sync_timeouts[i]);
        }
    }
}

int process_wait_is_pending(const Process* process) {
    Thread* thread = wait_thread_for_process((Process*)process);
    return thread_wait_is_pending(thread);
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
    process_surface_mappings_reset(process);
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
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    process->pid = pid;
    process->generation = process_next_generation();
    process->parent_pid = parent != 0 ? parent->pid : 0;
    process->parent_generation = parent != 0 ? parent->generation : 0;
    process->permissions = OS_PROCESS_PERMISSION_ALL;
    process->thread_count = 0;
    process->main_thread_identity.tid = 0;
    process->main_thread_identity.generation = 0;
    process->fault_thread_identity.tid = 0;
    process->fault_thread_identity.generation = 0;
    process->exiting = 0;
    kernel_spinlock_release(&process_lock, &token);
    if (allocate_thread_record(process, 1) == 0) {
        process->pid = 0;
        process->generation = 0;
        process->parent_pid = 0;
        process->parent_generation = 0;
    }
}

int process_has_permissions(const Process* process, uint32_t permissions) {
    return process != 0 &&
           (permissions & ~OS_PROCESS_PERMISSION_VALID_MASK) == 0 &&
           (process->permissions & permissions) == permissions;
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

int process_event_queue_has_key(const Process* process) {
    return process != 0 && input_event_queue_has_key(&process->event_queue);
}

int process_event_queue_has_character(const Process* process) {
    return process != 0 && input_event_queue_has_character(&process->event_queue);
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
    Thread* thread = wait_thread_for_process(process);
    if (thread != 0 && thread->context->wait_reason == PROCESS_WAIT_IPC) {
        process_wait_reset(process);
    }
}

int process_ipc_waiting(const Process* process) {
    return process_wait_count(process, PROCESS_WAIT_IPC) != 0;
}

void process_input_wait_begin(Process* process) {
    process_wait_begin(process, PROCESS_WAIT_INPUT, 0, 0, 0);
}

void process_input_wait_end(Process* process) {
    Thread* thread = wait_thread_for_process(process);
    if (thread != 0 &&
        (thread->context->wait_reason == PROCESS_WAIT_INPUT ||
         thread->context->wait_reason == PROCESS_WAIT_KEY ||
         thread->context->wait_reason == PROCESS_WAIT_CHAR)) {
        process_wait_reset(process);
    }
}

int process_input_waiting(const Process* process) {
    return process_wait_count(process, PROCESS_WAIT_INPUT) != 0 ||
           process_wait_count(process, PROCESS_WAIT_KEY) != 0 ||
           process_wait_count(process, PROCESS_WAIT_CHAR) != 0;
}

static int process_can_receive_focus(const Process* process) {
    if (process == 0 || process->pid == 0 || !process->active ||
        process->background) {
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

void thread_release_process_records(Process* owner) {
    if (owner == 0) {
        return;
    }
    uint64_t kernel_stacks[THREAD_TABLE_SIZE];
    uint32_t kernel_stack_count = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner != owner) {
            continue;
        }
        scheduler_remove_unlocked(thread);
        uint64_t kernel_stack = thread->context != 0
            ? thread->context->kernel_stack_base
            : 0;
        thread_record_zero(thread);
        if (kernel_stack != 0 && kernel_stack_count < THREAD_TABLE_SIZE) {
            kernel_stacks[kernel_stack_count++] = kernel_stack;
        }
    }
    owner->thread_count = 0;
    owner->main_thread_identity.tid = 0;
    owner->main_thread_identity.generation = 0;
    kernel_spinlock_release(&process_lock, &token);
    for (uint32_t i = 0; i < kernel_stack_count; i++) {
        pmm_free_block((void*)(uintptr_t)kernel_stacks[i]);
    }
}

void process_clear(Process* process) {
    if (process == 0) {
        return;
    }

    recover_display_session_for_process(process->pid, process->generation);
    process_surface_unmap_all(process);
    service_unregister_owner(process->pid);
    process_clear_focus(process->pid);
    if (process->pid != 0) {
        vfs_close_all_for_owner(process->pid);
    }
    if (process->handle_table.lock.lock_class == KERNEL_LOCK_CLASS_HANDLE) {
        kernel_object_release_table(&process->handle_table);
    }
    thread_release_process_records(process);
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    process->pid = 0;
    process->generation = 0;
    process->parent_pid = 0;
    process->parent_generation = 0;
    process->name[0] = '\0';
    process->code_base = 0;
    process->elf_link_base = 0;
    process->heap_base = 0;
    process->heap_break = 0;
    process->heap_mapped_end = 0;
    process->heap_limit = 0;
    process->entry_point = 0;
    process->image_size = 0;
    process->code_page_count = 0;
    process->elf_alias_page_count = 0;
    process->heap_page_count = 0;
    process->state = PROCESS_STATE_EMPTY;
    process->termination_reason = PROCESS_TERM_NONE;
    process->status_code = 0;
    process->slot_index = 0;
    process->shell_prompt_kind = SHELL_PROMPT_NONE;
    process->argc = 0;
    process->permissions = 0;
    process->active = 0;
    process->reaped = 0;
    process->background = 0;
    process->exiting = 0;
    process->reserved_process = 0;
    process->thread_count = 0;
    process->main_thread_identity.tid = 0;
    process->main_thread_identity.generation = 0;
    process->fault_thread_identity.tid = 0;
    process->fault_thread_identity.generation = 0;
    process->cwd[0] = '/';
    process->cwd[1] = '\0';
    process->command_line[0] = '\0';
    thread_context_reset(&process->main_thread_context);
    process_reset_address_space_record(process);
    kernel_handle_table_init(&process->handle_table);
    process_event_queue_reset(process);
    process_ipc_mailbox_reset(process);
    kernel_spinlock_release(&process_lock, &token);
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

    Thread* terminating_thread = current_thread();
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    uint32_t parent_pid = process->parent_pid;
    uint32_t parent_generation = process->parent_generation;
    uint32_t own_pid = process->pid;
    uint32_t own_generation = process->generation;

    process->exiting = 1;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner != process) {
            continue;
        }
        scheduler_remove_unlocked(thread);
        thread->active = 0;
        thread->exited = 1;
        thread->exit_code = status_code;
        thread->context->scheduler_state = SCHED_STATE_FINISHED;
        thread->context->pause_reason = PROCESS_PAUSE_NONE;
        thread->context->resumable = 0;
        thread_wait_reset_unlocked(thread);
    }
    process->state = final_state;
    process->termination_reason = reason;
    process->status_code = status_code;
    process->scheduler_state = SCHED_STATE_FINISHED;
    process->pause_reason = PROCESS_PAUSE_NONE;
    process->resumable = 0;
    process->active = 0;
    process->reaped = 0;
    if (input_focus_pid == process->pid) {
        input_focus_pid = 0;
    }
    process_cleanup_owned_children(own_pid, own_generation);
    reap_old_child_results(parent_pid,
                           parent_generation,
                           PROCESS_CHILD_RESULT_HISTORY_LIMIT);
    kernel_spinlock_release(&process_lock, &token);

    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->owner == process && thread != terminating_thread) {
            thread_release_runtime(thread);
        }
    }
    recover_display_session_for_process(own_pid, own_generation);
    vfs_close_all_for_owner(own_pid);
    process_surface_unmap_all(process);
    kernel_object_release_table(&process->handle_table);
    kernel_handle_table_init(&process->handle_table);
    service_unregister_owner(own_pid);
    process_event_queue_reset(process);
    process_ipc_mailbox_reset(process);

    ProcessIdentity parent_identity;
    parent_identity.pid = parent_pid;
    parent_identity.generation = parent_generation;
    Process* parent = find_process_by_identity(parent_identity);
    if (parent == 0 || parent->state == PROCESS_STATE_RETURNED ||
        parent->state == PROCESS_STATE_FAILED) {
        process->reaped = 1;
    }
    process_wait_signal(parent, PROCESS_WAIT_CHILD, PROCESS_WAIT_OK);
}

void process_mark_failed(Process* process, uint32_t reason, uint32_t status_code) {
    process_finish(process, PROCESS_STATE_FAILED, reason, status_code);
}

void process_mark_returned(Process* process, uint32_t reason, uint32_t status_code) {
    process_finish(process, PROCESS_STATE_RETURNED, reason, status_code);
}

static void thread_discard_record(Thread* thread) {
    if (thread == 0) {
        return;
    }
    Process* owner = thread->owner;
    uint64_t kernel_stack = thread->context != 0
        ? thread->context->kernel_stack_base
        : 0;
    KernelSpinlockToken token;
    if (kernel_spinlock_acquire(&process_lock, &token)) {
        scheduler_remove_unlocked(thread);
        if (owner != 0 && owner->thread_count > 0) {
            owner->thread_count--;
        }
        if (owner != 0 && thread->is_main) {
            owner->main_thread_identity.tid = 0;
            owner->main_thread_identity.generation = 0;
        }
        thread_record_zero(thread);
        kernel_spinlock_release(&process_lock, &token);
    }
    if (kernel_stack != 0) {
        pmm_free_block((void*)(uintptr_t)kernel_stack);
    }
}

static int thread_user_code_address_valid(const Process* process, uint64_t address) {
    if (process == 0 || address == 0 ||
        !address_space_owns_address(&process->address_space, address)) {
        return 0;
    }
    uint64_t flags = address_space_get_flags(&process->address_space, address);
    return (flags & VM_FLAG_USER) != 0 && (flags & VM_FLAG_NO_EXECUTE) == 0;
}

static int thread_stack_slot_available(const Process* owner, uint32_t slot) {
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        const Thread* thread = &thread_table[i];
        if (thread->owner == owner && thread->tid != 0 && !thread->is_main &&
            thread->user_stack_slot == slot) {
            return 0;
        }
    }
    return 1;
}

int thread_create_user(Process* owner,
                       uint64_t entry,
                       uint64_t argument,
                       uint64_t return_trampoline,
                       uint32_t stack_size,
                       uint32_t flags,
                       ThreadIdentity* identity_out) {
    if (owner == 0 || identity_out == 0 || !owner->active || owner->exiting ||
        flags != OS_THREAD_CREATE_FLAG_NONE ||
        stack_size < OS_THREAD_STACK_MIN || stack_size > OS_THREAD_STACK_MAX ||
        (stack_size & (VM_PAGE_SIZE - 1U)) != 0 ||
        !thread_user_code_address_valid(owner, entry) ||
        !thread_user_code_address_valid(owner, return_trampoline)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }

    uint32_t stack_slot = UINT32_MAX;
    for (uint32_t slot = 0; slot < THREADS_PER_PROCESS_MAX - 1; slot++) {
        if (thread_stack_slot_available(owner, slot)) {
            stack_slot = slot;
            break;
        }
    }
    if (stack_slot == UINT32_MAX) {
        return SYS_ERR_NO_RESOURCES;
    }

    Thread* thread = allocate_thread_record(owner, 0);
    if (thread == 0) {
        return SYS_ERR_NO_RESOURCES;
    }
    thread->user_stack_slot = stack_slot;

    const uint64_t stack_slot_span =
        (uint64_t)(1U + (OS_THREAD_STACK_MAX / VM_PAGE_SIZE)) * VM_PAGE_SIZE;
    const uint64_t arena_size =
        (uint64_t)(THREADS_PER_PROCESS_MAX - 1) * stack_slot_span;
    uint64_t slot_end = owner->code_base + USER_SLOT_SPAN;
    uint64_t guard_base = slot_end - arena_size +
        (uint64_t)stack_slot * stack_slot_span;
    uint64_t stack_base = guard_base + VM_PAGE_SIZE;
    uint32_t page_count = stack_size / VM_PAGE_SIZE;
    uint32_t mapped = 0;

    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t phys =
            kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_THREAD_USER_STACK)
                ? 0
                : (uint64_t)(uintptr_t)pmm_alloc_block();
        if (phys == 0 ||
            kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_THREAD_MAPPING) ||
            !address_space_map_page(&owner->address_space,
                                    stack_base + (uint64_t)page * VM_PAGE_SIZE,
                                    phys,
                                    VM_FLAG_WRITABLE | VM_FLAG_USER | VM_FLAG_NO_EXECUTE)) {
            if (phys != 0) {
                pmm_free_block((void*)(uintptr_t)phys);
            }
            address_space_unmap_free_range(&owner->address_space, stack_base, mapped);
            thread_discard_record(thread);
            return SYS_ERR_OUT_OF_MEMORY;
        }
        for (uint64_t offset = 0; offset < VM_PAGE_SIZE; offset++) {
            *((volatile uint8_t*)(uintptr_t)(phys + offset)) = 0;
        }
        mapped++;
    }
    if (!address_space_add_region(&owner->address_space,
                                  stack_base,
                                  stack_size,
                                  ADDRESS_SPACE_REGION_READ | ADDRESS_SPACE_REGION_WRITE)) {
        address_space_unmap_free_range(&owner->address_space, stack_base, mapped);
        thread_discard_record(thread);
        return SYS_ERR_NO_RESOURCES;
    }

    uint64_t initial_rsp = stack_base + stack_size - sizeof(uint64_t);
    uint64_t return_phys = address_space_get_phys(&owner->address_space, initial_rsp);
    if (return_phys == 0) {
        address_space_unmap_free_range(&owner->address_space, stack_base, mapped);
        thread_discard_record(thread);
        return SYS_ERR_IO;
    }
    *((volatile uint64_t*)(uintptr_t)return_phys) = return_trampoline;

    ThreadContext* context = thread->context;
    context->stack_guard_base = guard_base;
    context->stack_base = stack_base;
    context->stack_guard_page_count = 1;
    context->stack_page_count = page_count;
    context->saved_rip = entry;
    context->saved_rsp = initial_rsp;
    context->saved_rdi = argument;
    context->saved_rflags = 0x202;
    context->resumable = 1;
    context->pause_reason = PROCESS_PAUSE_YIELD;
    context->scheduler_state = SCHED_STATE_READY;
    scheduler_enqueue_thread(thread);
    *identity_out = thread_identity(thread);
    return 0;
}

static void thread_join_release_claim_unlocked(Thread* waiter) {
    if (waiter == 0 || waiter->context == 0 ||
        waiter->context->wait_target_tid == 0) {
        return;
    }
    ThreadIdentity target_identity;
    target_identity.tid = waiter->context->wait_target_tid;
    target_identity.generation = waiter->context->wait_target_generation;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* target = &thread_table[i];
        if (thread_identity_matches(target, target_identity) &&
            target->join_owner_tid == waiter->tid &&
            target->join_owner_generation == waiter->generation) {
            target->join_owner_tid = 0;
            target->join_owner_generation = 0;
            return;
        }
    }
}

void thread_mark_exited(Thread* thread, uint32_t exit_code) {
    if (thread == 0 || thread->owner == 0) {
        return;
    }
    Process* owner = thread->owner;
    if (kernel_sync_thread_exit != 0) {
        kernel_sync_thread_exit(thread);
    }
    int remaining = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    if (!thread->active || thread->exited) {
        kernel_spinlock_release(&process_lock, &token);
        return;
    }
    scheduler_remove_unlocked(thread);
    thread->active = 0;
    thread->exited = 1;
    thread->exit_code = exit_code;
    thread->context->scheduler_state = SCHED_STATE_FINISHED;
    thread->context->resumable = 0;
    thread->context->pause_reason = PROCESS_PAUSE_NONE;
    thread_wait_reset_unlocked(thread);

    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* candidate = &thread_table[i];
        if (candidate->owner == owner && candidate->active) {
            remaining++;
        }
        if (candidate->tid == thread->join_owner_tid &&
            candidate->generation == thread->join_owner_generation) {
            thread_wait_signal_unlocked(candidate,
                                        PROCESS_WAIT_THREAD_JOIN,
                                        PROCESS_WAIT_OK);
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    if (remaining == 0 && owner->active && !owner->exiting) {
        process_mark_returned(owner, PROCESS_TERM_EXIT, exit_code);
    }
}

void thread_release_runtime(Thread* thread) {
    if (thread == 0 || thread->context == 0 || thread->runtime_released) {
        return;
    }
    ThreadContext* context = thread->context;
    if (thread->owner != 0 && context->stack_base != 0 &&
        context->stack_page_count != 0) {
        address_space_unmap_free_range(&thread->owner->address_space,
                                       context->stack_base,
                                       context->stack_page_count);
    }
    context->stack_base = 0;
    context->stack_guard_base = 0;
    context->stack_page_count = 0;
    context->stack_guard_page_count = 0;
    if (context->kernel_stack_base != 0) {
        pmm_free_block((void*)(uintptr_t)context->kernel_stack_base);
        context->kernel_stack_base = 0;
        context->kernel_stack_page_count = 0;
    }
    thread->runtime_released = 1;
    if (thread->join_consumed && !thread->is_main) {
        thread_discard_record(thread);
    }
}

int thread_join_begin(Thread* caller,
                      ThreadIdentity target_identity,
                      uint64_t user_status_address,
                      uint32_t timeout_ticks,
                      uint32_t tick_now,
                      uint32_t* immediate_status) {
    if (caller == 0 || caller->owner == 0 || immediate_status == 0 ||
        target_identity.tid == 0 || target_identity.generation == 0 ||
        thread_identity_matches(caller, target_identity)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    Thread* target = 0;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        if (thread_identity_matches(&thread_table[i], target_identity)) {
            target = &thread_table[i];
            break;
        }
    }
    if (target == 0 || target->owner != caller->owner) {
        kernel_spinlock_release(&process_lock, &token);
        return SYS_ERR_NOT_FOUND;
    }
    if (target->join_consumed || target->join_owner_tid != 0) {
        kernel_spinlock_release(&process_lock, &token);
        return SYS_ERR_ALREADY_EXISTS;
    }
    target->join_owner_tid = caller->tid;
    target->join_owner_generation = caller->generation;
    if (target->exited) {
        *immediate_status = target->exit_code;
        target->join_consumed = 1;
        kernel_spinlock_release(&process_lock, &token);
        if (target->runtime_released && !target->is_main) {
            thread_discard_record(target);
        }
        return 1;
    }

    ThreadContext* context = caller->context;
    scheduler_remove_unlocked(caller);
    context->wait_pending = 1;
    context->wait_has_deadline = timeout_ticks != 0 ? 1 : 0;
    context->wait_reason = PROCESS_WAIT_THREAD_JOIN;
    context->wait_result = PROCESS_WAIT_OK;
    context->wait_deadline = timeout_ticks != 0 ? tick_now + timeout_ticks : 0;
    context->wake_tick = context->wait_deadline;
    context->wait_user_address = user_status_address;
    context->wait_target_tid = target_identity.tid;
    context->wait_target_generation = target_identity.generation;
    context->scheduler_state = SCHED_STATE_WAITING;
    kernel_spinlock_release(&process_lock, &token);
    return 0;
}

int thread_join_consume(Thread* caller, uint32_t* status_out) {
    if (caller == 0 || caller->context == 0 || status_out == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    ThreadIdentity target_identity;
    target_identity.tid = caller->context->wait_target_tid;
    target_identity.generation = caller->context->wait_target_generation;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    Thread* target = 0;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        if (thread_identity_matches(&thread_table[i], target_identity)) {
            target = &thread_table[i];
            break;
        }
    }
    if (target == 0 || !target->exited ||
        target->join_owner_tid != caller->tid ||
        target->join_owner_generation != caller->generation) {
        kernel_spinlock_release(&process_lock, &token);
        return SYS_ERR_NOT_FOUND;
    }
    *status_out = target->exit_code;
    target->join_consumed = 1;
    int discard = target->runtime_released && !target->is_main;
    kernel_spinlock_release(&process_lock, &token);
    if (discard) {
        thread_discard_record(target);
    }
    return 0;
}

static int scheduler_queue_contains(const Thread* thread) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        if (sched_queue[index] == thread) {
            return 1;
        }
    }
    return 0;
}

static void scheduler_enqueue_unlocked(Thread* thread) {
    if (thread == 0 || thread->context == 0 || !thread->active ||
        thread->owner == 0 || !thread->owner->active) {
        return;
    }
    if (scheduler_queue_contains(thread)) {
        thread->context->scheduler_state = SCHED_STATE_READY;
        thread->context->timeslice_ticks = thread_quantum(thread);
        return;
    }
    if (sched_queue_count >= SCHED_QUEUE_SIZE) {
        return;
    }

    uint32_t index = (sched_queue_head + sched_queue_count) % SCHED_QUEUE_SIZE;
    sched_queue[index] = thread;
    sched_queue_count++;
    thread->context->scheduler_state = SCHED_STATE_READY;
    thread->context->timeslice_ticks = thread_quantum(thread);
}

void scheduler_enqueue_thread(Thread* thread) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    scheduler_enqueue_unlocked(thread);
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_enqueue(Process* process) {
    scheduler_enqueue_thread(process_main_thread(process));
}

static void scheduler_remove_unlocked(Thread* thread) {
    if (thread == 0 || sched_queue_count == 0) {
        return;
    }

    Thread* compacted[SCHED_QUEUE_SIZE];
    uint32_t kept = 0;
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        if (sched_queue[index] != thread) {
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

void scheduler_remove_thread(Thread* thread) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    scheduler_remove_unlocked(thread);
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_remove(Process* process) {
    scheduler_remove_thread(process_main_thread(process));
}

void scheduler_mark_thread_running(Thread* thread) {
    if (thread == 0 || thread->context == 0 || thread->owner == 0) {
        return;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    if (thread->context->wait_reason == PROCESS_WAIT_CHILD) {
        thread_wait_reset_unlocked(thread);
    }
    thread->context->scheduler_state = SCHED_STATE_RUNNING;
    thread->context->pause_reason = PROCESS_PAUSE_NONE;
    thread->context->wake_tick = 0;
    thread->context->timeslice_ticks = thread_quantum(thread);
    sched_last_pid = thread->owner_pid;
    sched_switch_count++;
    thread->context->switch_count++;
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_mark_running(Process* process) {
    scheduler_mark_thread_running(process_main_thread(process));
}

void scheduler_mark_waiting(Process* process) {
    Thread* thread = wait_thread_for_process(process);
    if (thread == 0) {
        return;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    thread->context->scheduler_state = SCHED_STATE_WAITING;
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_mark_sleeping(Process* process, uint32_t wake_tick) {
    Thread* thread = wait_thread_for_process(process);
    if (thread == 0) {
        return;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    scheduler_remove_unlocked(thread);
    thread->context->scheduler_state = SCHED_STATE_WAITING;
    thread->context->wake_tick = wake_tick;
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_mark_thread_finished(Thread* thread) {
    if (thread == 0 || thread->context == 0) {
        return;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    scheduler_remove_unlocked(thread);
    thread->context->scheduler_state = SCHED_STATE_FINISHED;
    thread->context->timeslice_ticks = 0;
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_mark_finished(Process* process) {
    scheduler_mark_thread_finished(process_main_thread(process));
}

void scheduler_yield_current() {
    Thread* thread = current_thread();
    if (thread == 0 || thread->context == 0) {
        return;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    sched_yield_count++;
    thread->context->yield_count++;
    thread->context->scheduler_state = SCHED_STATE_READY;
    thread->context->timeslice_ticks = thread_quantum(thread);
    scheduler_remove_unlocked(thread);
    scheduler_enqueue_unlocked(thread);
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_on_tick() {
    Thread* thread = current_thread();
    if (thread == 0 || thread->context == 0) {
        return;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    thread->context->runtime_ticks++;
    thread->context->runtime_ticks_total++;
    if (thread->context->timeslice_ticks > 0) {
        thread->context->timeslice_ticks--;
    }
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_note_preemption(Thread* thread) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    if (thread != 0 && thread->context != 0) {
        thread->context->preemption_count++;
    }
    kernel_spinlock_release(&process_lock, &token);
}

void scheduler_refresh_timeslice(Thread* thread) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return;
    }
    if (thread != 0 && thread->context != 0) {
        thread->context->timeslice_ticks = thread_quantum(thread);
    }
    kernel_spinlock_release(&process_lock, &token);
}

int thread_get_info(const Process* requester,
                    ThreadIdentity identity,
                    OsThreadInfo* info) {
    if (requester == 0 || info == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    Thread* thread = 0;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        if (thread_identity_matches(&thread_table[i], identity) &&
            thread_table[i].owner == requester) {
            thread = &thread_table[i];
            break;
        }
    }
    if (thread == 0 || thread->context == 0) {
        kernel_spinlock_release(&process_lock, &token);
        return SYS_ERR_NOT_FOUND;
    }
    ThreadContext* context = thread->context;
    info->size = sizeof(OsThreadInfo);
    info->scheduler_state = context->scheduler_state;
    info->identity.tid = thread->tid;
    info->identity.generation = thread->generation;
    info->owner_pid = thread->owner_pid;
    info->owner_generation = thread->owner_generation;
    info->wait_reason = context->wait_reason;
    info->priority = thread->priority;
    info->timeslice_remaining = context->timeslice_ticks;
    info->queue_position = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        if (sched_queue[index] == thread) {
            info->queue_position = i;
            break;
        }
    }
    info->tls_base = context->tls_base;
    info->runtime_ticks = context->runtime_ticks_total;
    info->preemption_count = context->preemption_count;
    info->yield_count = context->yield_count;
    info->block_count = context->block_count;
    info->wake_count = context->wake_count;
    info->switch_count = context->switch_count;
    kernel_spinlock_release(&process_lock, &token);
    return 0;
}

int thread_set_priority(Process* requester,
                        ThreadIdentity identity,
                        uint32_t priority) {
    if (requester == 0 || priority > OS_THREAD_PRIORITY_MAX) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    int result = SYS_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread_identity_matches(thread, identity) && thread->owner == requester) {
            thread->priority = priority;
            if (thread->context != 0 &&
                thread->context->timeslice_ticks > thread_quantum(thread)) {
                thread->context->timeslice_ticks = thread_quantum(thread);
            }
            result = 0;
            break;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

void scheduler_wake_sleeping_processes(uint32_t tick_now) {
    process_wait_tick(tick_now);
}

static Thread* find_next_ready_thread_unlocked(ThreadIdentity exclude,
                                                uint32_t exclude_pid,
                                                int background_only,
                                                int sleeping_only) {
    for (uint32_t i = 0; i < sched_queue_count; i++) {
        uint32_t index = (sched_queue_head + i) % SCHED_QUEUE_SIZE;
        Thread* thread = sched_queue[index];
        if (thread == 0 || thread->owner == 0 || thread->tid == 0) {
            continue;
        }
        if (thread_identity_matches(thread, exclude) ||
            (exclude_pid != 0 && thread->owner_pid == exclude_pid)) {
            continue;
        }
        if (!thread->active || !thread->owner->active ||
            !thread->context->resumable) {
            continue;
        }
        if (thread->context->scheduler_state != SCHED_STATE_READY) {
            continue;
        }
        if (background_only && !thread->owner->background) {
            continue;
        }
        if (sleeping_only && thread->context->pause_reason != PROCESS_PAUSE_SLEEP) {
            continue;
        }
        return thread;
    }

    // The thread table is authoritative. Recover a READY thread missing from
    // the bounded queue so an IRQ wakeup cannot be lost.
    for (uint32_t i = 0; i < THREAD_TABLE_SIZE; i++) {
        Thread* thread = &thread_table[i];
        if (thread->tid == 0 || thread->owner == 0 ||
            thread_identity_matches(thread, exclude) ||
            (exclude_pid != 0 && thread->owner_pid == exclude_pid) ||
            !thread->active || !thread->owner->active ||
            !thread->context->resumable ||
            thread->context->scheduler_state != SCHED_STATE_READY ||
            (background_only && !thread->owner->background) ||
            (sleeping_only && thread->context->pause_reason != PROCESS_PAUSE_SLEEP)) {
            continue;
        }
        scheduler_enqueue_unlocked(thread);
        return thread;
    }
    return 0;
}

Thread* find_next_ready_thread(ThreadIdentity exclude) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    Thread* result = find_next_ready_thread_unlocked(exclude, 0, 0, 0);
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

Process* find_next_ready_process(uint32_t exclude_pid) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    ThreadIdentity none = {0, 0};
    Thread* thread = find_next_ready_thread_unlocked(none, exclude_pid, 0, 0);
    Process* result = thread != 0 ? thread->owner : 0;
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

Process* find_next_background_ready_process(uint32_t exclude_pid) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    ThreadIdentity none = {0, 0};
    Thread* thread = find_next_ready_thread_unlocked(none, exclude_pid, 1, 0);
    Process* result = thread != 0 ? thread->owner : 0;
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

Process* find_next_woken_process(uint32_t exclude_pid) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    ThreadIdentity none = {0, 0};
    Thread* thread = find_next_ready_thread_unlocked(none, exclude_pid, 0, 1);
    Process* result = thread != 0 ? thread->owner : 0;
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

int scheduler_should_preempt_current() {
    Thread* thread = current_thread();
    if (thread == 0 || thread->context == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    if (thread->context->scheduler_state != SCHED_STATE_RUNNING) {
        kernel_spinlock_release(&process_lock, &token);
        return 0;
    }
    if (thread->context->timeslice_ticks != 0) {
        kernel_spinlock_release(&process_lock, &token);
        return 0;
    }
    int result = find_next_ready_thread_unlocked(thread_identity(thread), 0, 0, 0) != 0;
    kernel_spinlock_release(&process_lock, &token);
    return result;
}

int process_record_is_active(const Process* process) {
    if (process == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < EXECUTION_STACK_SIZE; i++) {
        if (process_stack[i] == process) {
            return 1;
        }
    }
    return 0;
}

Process* allocate_process_record() {
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_PROCESS)) {
        return 0;
    }
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
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_table[i].pid == pid) {
            Process* result = &process_table[i];
            kernel_spinlock_release(&process_lock, &token);
            return result;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return 0;
}

Process* find_process_by_identity(ProcessIdentity identity) {
    if (identity.pid == 0 || identity.generation == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&process_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; i < PROCESS_TABLE_SIZE; i++) {
        if (process_identity_matches(&process_table[i], identity)) {
            Process* result = &process_table[i];
            kernel_spinlock_release(&process_lock, &token);
            return result;
        }
    }
    kernel_spinlock_release(&process_lock, &token);
    return 0;
}

Process* find_process_by_identity_compat(uint32_t pid, uint32_t generation) {
    ProcessIdentity identity;
    identity.pid = pid;
    identity.generation = generation;
    return generation == 0 ? find_process_by_pid(pid) : find_process_by_identity(identity);
}
