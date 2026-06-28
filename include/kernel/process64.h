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

extern uint32_t user_program_depth;
extern uint32_t next_pid;
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

void process_clear(Process* process);
void process_mark_failed(Process* process, uint32_t reason, uint32_t status_code);
void process_mark_returned(Process* process, uint32_t reason, uint32_t status_code);
void process_event_queue_reset(Process* process);
int process_event_queue_push(Process* process, const OsInputEvent* event);
int process_event_queue_pop(Process* process, OsInputEvent* event);
uint32_t process_event_queue_count(const Process* process);
uint32_t process_event_queue_delivered_count(const Process* process);
uint32_t process_event_queue_dropped_count(const Process* process);
void process_input_wait_begin(Process* process);
void process_input_wait_end(Process* process);
int process_input_waiting(const Process* process);
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
