#ifndef KERNEL_PROCESS64_H
#define KERNEL_PROCESS64_H

#include <stdint.h>

#include "kernel/process.h"

#define USER_PROGRAM_SLOT_COUNT 4
#define PROCESS_TABLE_SIZE 8
#define PROCESS_CHILD_RESULT_HISTORY_LIMIT 3
#define SCHED_QUEUE_SIZE PROCESS_TABLE_SIZE
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
    uint32_t handle_count;
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
    uint32_t queue_pids[SCHED_QUEUE_SIZE];
    ProcessDiagnosticSnapshot processes[PROCESS_TABLE_SIZE];
};

extern uint32_t user_program_depth;
extern uint32_t next_pid;
extern uint32_t next_process_generation;
extern Process process_table[PROCESS_TABLE_SIZE];
extern Process* process_stack[USER_PROGRAM_SLOT_COUNT];
extern Process* sched_queue[SCHED_QUEUE_SIZE];
extern uint32_t sched_queue_count;
extern uint32_t sched_queue_head;
extern uint32_t sched_last_pid;
extern uint32_t sched_switch_count;
extern uint32_t sched_yield_count;
extern uint32_t input_focus_pid;

Process* current_process();
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
int process_wait_signal(Process* process, uint32_t reason, int32_t result);
int process_wait_cancel(Process* process, uint32_t reason, int32_t result);
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
void scheduler_on_tick();
void scheduler_wake_sleeping_processes(uint32_t tick_now);
int scheduler_should_preempt_current();

int process_record_is_active(const Process* process);
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
