#ifndef KERNEL_KERNEL_DIAG_H
#define KERNEL_KERNEL_DIAG_H

#include <stdint.h>

#include "kernel/process.h"

const char* process_state_name(uint32_t state);
const char* process_term_name(uint32_t reason);
const char* scheduler_state_name(uint32_t state);
const char* pause_reason_name(uint32_t reason);

void print_process_summary(const Process* process, uint32_t tick_now);
void print_process_table(uint32_t tick_now);
void print_job_compact(const char* label, const Process* process, uint32_t tick_now);
void print_child_result_compact(const char* label, const Process* process);
void print_jobs_for_process(const Process* parent, uint32_t tick_now);
void print_scheduler_info(Process* const* sched_queue,
                          uint32_t sched_queue_count,
                          uint32_t sched_queue_head,
                          uint32_t sched_queue_capacity,
                          uint32_t sched_last_pid,
                          uint32_t sched_switch_count,
                          uint32_t sched_yield_count,
                          uint32_t focused_pid,
                          uint32_t tick_now);
void print_vfs_mounts();

#endif
