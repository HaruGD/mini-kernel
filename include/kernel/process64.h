#ifndef KERNEL_PROCESS64_H
#define KERNEL_PROCESS64_H

#include <stdint.h>

#include "kernel/process.h"

#define USER_PROGRAM_SLOT_COUNT 16
#define PROCESS_TABLE_SIZE 16
#define PROCESS_CHILD_RESULT_HISTORY_LIMIT 3
#define THREADS_PER_PROCESS_MAX 8
#define THREAD_TABLE_SIZE (PROCESS_TABLE_SIZE * THREADS_PER_PROCESS_MAX)
#define EXECUTION_STACK_SIZE USER_PROGRAM_SLOT_COUNT
#define SCHED_QUEUE_SIZE THREAD_TABLE_SIZE
#define SCHED_DEFAULT_TIMESLICE 6
#define USER_SLOT_SPAN 0x00200000ULL
#define USER_HEAP_OFFSET 0x00110000ULL

struct ProcessDiagnosticSnapshot {
    uint32_t pid;
    uint32_t generation;
    uint32_t parent_pid;
    uint32_t parent_generation;
    char name[PROCESS_NAME_MAX];
    uint32_t slot_index;
    uint32_t state;
    uint32_t termination_reason;
    uint32_t status_code;
    uint32_t scheduler_state;
    uint32_t pause_reason;
    uint32_t wait_reason;
    uint32_t permissions;
    uint32_t runtime_ticks;
    uint32_t timeslice_ticks;
    uint32_t wake_tick;
    uint32_t wait_deadline;
    uint32_t mapping_count;
    uint32_t handle_count;
    uint32_t thread_count;
    uint32_t main_tid;
    uint32_t main_thread_generation;
    ThreadIdentity fault_thread_identity;
    uint64_t thread_runtime_ticks;
    uint64_t thread_preemption_count;
    uint64_t thread_yield_count;
    uint64_t thread_block_count;
    uint64_t thread_wake_count;
    uint64_t thread_switch_count;
    KernelIpcMailboxStats mailbox;
    uint8_t active;
    uint8_t reaped;
    uint8_t resumable;
    uint8_t background;
    uint8_t wait_pending;
    uint8_t wait_has_deadline;
    uint8_t reserved[2];
};

struct SchedulerDiagnosticSnapshot {
    uint32_t process_count;
    uint32_t queue_count;
    uint32_t queue_head;
    uint32_t last_pid;
    uint32_t switch_count;
    uint32_t yield_count;
    uint32_t focused_pid;
    uint32_t thread_count;
    uint32_t queue_pids[SCHED_QUEUE_SIZE];
    uint32_t queue_tids[SCHED_QUEUE_SIZE];
    uint32_t queue_thread_generations[SCHED_QUEUE_SIZE];
    uint32_t queue_scheduler_states[SCHED_QUEUE_SIZE];
    uint32_t queue_runtime_ticks[SCHED_QUEUE_SIZE];
    ProcessDiagnosticSnapshot processes[PROCESS_TABLE_SIZE];
};

extern uint32_t next_pid;
extern uint32_t next_process_generation;
extern uint32_t next_tid;
extern uint32_t next_thread_generation;
extern uint64_t next_wait_sequence;
extern Process process_table[PROCESS_TABLE_SIZE];
extern Thread thread_table[THREAD_TABLE_SIZE];
extern Thread* sched_queue[SCHED_QUEUE_SIZE];
extern uint32_t sched_queue_count;
extern uint32_t sched_queue_head;
extern uint32_t sched_last_pid;
extern uint32_t sched_switch_count;
extern uint32_t sched_yield_count;
extern uint32_t input_focus_pid;

Process* current_process();
Thread* current_thread();
uint32_t process_execution_depth();
void process_execution_reset();
int process_execution_push(Process* process,
                           Thread* thread,
                           uint32_t* stack_index);
int process_execution_pop(uint32_t stack_index,
                          Process* process,
                          Thread* thread);
Thread* process_main_thread(const Process* process);
ThreadIdentity thread_identity(const Thread* thread);
int thread_identity_matches(const Thread* thread, ThreadIdentity identity);
Thread* find_thread_by_identity(ThreadIdentity identity);
Thread* allocate_thread_record(Process* owner, int is_main);
void thread_release_process_records(Process* owner);
void thread_context_reset(ThreadContext* context);
int thread_create_user(Process* owner,
                       uint64_t entry,
                       uint64_t argument,
                       uint64_t return_trampoline,
                       uint32_t stack_size,
                       uint32_t flags,
                       ThreadIdentity* identity_out);
void thread_mark_exited(Thread* thread, uint32_t exit_code);
void thread_release_runtime(Thread* thread);
int thread_join_begin(Thread* caller,
                      ThreadIdentity target,
                      uint64_t user_status_address,
                      uint32_t timeout_ticks,
                      uint32_t tick_now,
                      uint32_t* immediate_status);
int thread_join_consume(Thread* caller, uint32_t* status_out);
void process_system_init();
void process_get_diagnostic_snapshot(SchedulerDiagnosticSnapshot* snapshot);

void process_clear(Process* process);
ProcessIdentity process_identity(const Process* process);
int process_identity_matches(const Process* process, ProcessIdentity identity);
void process_assign_identity(Process* process, uint32_t pid, const Process* parent);
int process_has_permissions(const Process* process, uint32_t permissions);
Process* find_process_by_identity(ProcessIdentity identity);
Process* find_process_by_identity_compat(uint32_t pid, uint32_t generation);
void process_mark_failed(Process* process, uint32_t reason, uint32_t status_code);
void process_mark_returned(Process* process, uint32_t reason, uint32_t status_code);
void process_event_queue_reset(Process* process);
int process_event_queue_push(Process* process, const OsInputEvent* event);
int process_event_queue_pop(Process* process, OsInputEvent* event);
uint32_t process_event_queue_count(const Process* process);
uint32_t process_event_queue_delivered_count(const Process* process);
uint32_t process_event_queue_dropped_count(const Process* process);
int process_event_queue_has_key(const Process* process);
int process_event_queue_has_character(const Process* process);
void process_ipc_mailbox_reset(Process* process);
int process_ipc_mailbox_push(Process* process, const OsIpcMessage* message);
int process_ipc_mailbox_pop(Process* process, OsIpcMessage* message);
int process_ipc_mailbox_push_v2(Process* process, const OsIpcMessageV2* message);
int process_ipc_mailbox_pop_v2(Process* process, OsIpcMessageV2* message);
uint32_t process_ipc_mailbox_count(const Process* process);
uint32_t process_ipc_mailbox_delivered_count(const Process* process);
uint32_t process_ipc_mailbox_dropped_count(const Process* process);
void process_ipc_wait_begin(Process* process);
void process_ipc_wait_end(Process* process);
int process_ipc_waiting(const Process* process);
void process_input_wait_begin(Process* process);
void process_input_wait_end(Process* process);
int process_input_waiting(const Process* process);
void process_wait_reset(Process* process);
int process_wait_begin(Process* process,
                       uint32_t reason,
                       uint64_t user_address,
                       uint32_t timeout_ticks,
                       uint32_t tick_now);
int thread_wait_begin(Thread* thread,
                      uint32_t reason,
                      uint64_t user_address,
                      uint32_t timeout_ticks,
                      uint32_t tick_now);
void thread_wait_reset(Thread* thread);
int thread_wait_signal(Thread* thread, uint32_t reason, int32_t result);
int thread_wait_is_pending(const Thread* thread);
Thread* thread_wait_find_oldest(Process* process,
                                uint32_t reason,
                                uint64_t object_id);
int thread_wait_set_objects(Thread* thread,
                            uint64_t object_id,
                            uint64_t aux_object_id);
int thread_wait_retarget(Thread* thread,
                         uint32_t old_reason,
                         uint32_t new_reason,
                         uint64_t object_id,
                         uint64_t aux_object_id,
                         int32_t resume_result);
int thread_wait_cancel_object(Process* process,
                              uint64_t object_id,
                              int32_t result);
uint32_t process_wait_count(const Process* process, uint32_t reason);
int process_wait_signal(Process* process, uint32_t reason, int32_t result);
int process_wait_cancel(Process* process, uint32_t reason, int32_t result);
void process_notify_queued_ipc(Process* process);
void process_wait_tick(uint32_t tick_now);
int process_wait_is_pending(const Process* process);
const char* process_wait_reason_name(uint32_t reason);
uint32_t process_focused_pid();
Process* process_focused();
int process_set_focus(uint32_t pid);
void process_clear_focus(uint32_t pid);
const char* process_get_cwd(const Process* process);
void process_copy_cwd(Process* process, const char* cwd);

void scheduler_enqueue(Process* process);
void scheduler_remove(Process* process);
void scheduler_mark_running(Process* process);
void scheduler_mark_waiting(Process* process);
void scheduler_mark_sleeping(Process* process, uint32_t wake_tick);
void scheduler_mark_finished(Process* process);
void scheduler_yield_current();
void scheduler_complete_kernel_return(Thread* thread);
void scheduler_note_preemption(Thread* thread);
void scheduler_refresh_timeslice(Thread* thread);
int thread_get_info(const Process* requester,
                    ThreadIdentity identity,
                    OsThreadInfo* info);
int thread_set_priority(Process* requester,
                        ThreadIdentity identity,
                        uint32_t priority);
int thread_set_affinity(Process* requester,
                        ThreadIdentity identity,
                        uint32_t affinity_mask);
uint32_t scheduler_online_cpu_mask();
void scheduler_on_tick();
void scheduler_wake_sleeping_processes(uint32_t tick_now);
int scheduler_should_preempt_current();
int scheduler_should_reschedule_current();
void scheduler_enqueue_thread(Thread* thread);
void scheduler_remove_thread(Thread* thread);
void scheduler_mark_thread_running(Thread* thread);
void scheduler_mark_thread_finished(Thread* thread);
Thread* find_next_ready_thread(ThreadIdentity exclude);
Thread* scheduler_claim_ready_thread(ThreadIdentity exclude,
                                     uint32_t exclude_pid,
                                     int background_only,
                                     int sleeping_only);

int process_record_is_active(const Process* process);
int process_has_running_threads(const Process* process);
Process* allocate_process_record();
const Process* find_last_child_process(uint32_t parent_pid);
Process* find_waitable_child_process(uint32_t parent_pid);
uint32_t reap_all_child_processes(uint32_t parent_pid);
uint32_t count_unfinished_child_processes(uint32_t parent_pid);
Process* find_last_paused_child_process(uint32_t parent_pid);
Process* find_next_ready_process(uint32_t exclude_pid);
Process* find_next_background_ready_process(uint32_t exclude_pid);
Process* find_next_woken_process(uint32_t exclude_pid);
Process* find_process_by_pid(uint32_t pid);

#endif
