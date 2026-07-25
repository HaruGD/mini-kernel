#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

#include <stdint.h>

#include "os64/thread_types.h"

struct Process;

#define THREAD_CPU_INVALID (-1)
#define THREAD_AFFINITY_ALL 0x000000FFu

#define THREAD_CONTEXT_FIELDS \
    uint64_t stack_guard_base; \
    uint64_t stack_base; \
    uint64_t kernel_stack_base; \
    uint64_t tls_base; \
    uint64_t runtime_ticks_total; \
    uint64_t preemption_count; \
    uint64_t yield_count; \
    uint64_t block_count; \
    uint64_t wake_count; \
    uint64_t switch_count; \
    uint32_t stack_guard_page_count; \
    uint32_t stack_page_count; \
    uint32_t kernel_stack_page_count; \
    uint32_t scheduler_state; \
    uint32_t runtime_ticks; \
    uint32_t timeslice_ticks; \
    uint32_t wake_tick; \
    uint8_t resumable; \
    uint8_t pause_reason; \
    uint8_t wait_pending; \
    uint8_t wait_has_deadline; \
    uint8_t wait_reserved[4]; \
    uint32_t wait_reason; \
    int32_t wait_result; \
    uint32_t wait_deadline; \
    uint64_t wait_sequence; \
    uint64_t wait_user_address; \
    uint64_t wait_object_id; \
    uint64_t wait_aux_object_id; \
    uint32_t wait_target_tid; \
    uint32_t wait_target_generation; \
    uint64_t saved_rax; \
    uint64_t saved_rbx; \
    uint64_t saved_rcx; \
    uint64_t saved_rdx; \
    uint64_t saved_rbp; \
    uint64_t saved_rsi; \
    uint64_t saved_rdi; \
    uint64_t saved_r8; \
    uint64_t saved_r9; \
    uint64_t saved_r10; \
    uint64_t saved_r11; \
    uint64_t saved_r12; \
    uint64_t saved_r13; \
    uint64_t saved_r14; \
    uint64_t saved_r15; \
    uint64_t saved_rip; \
    uint64_t saved_rsp; \
    uint64_t saved_rflags; \
    alignas(16) uint8_t fx_state[512]; \
    uint8_t fx_initialized; \
    uint8_t fx_reserved[15]

struct alignas(16) ThreadContext {
    THREAD_CONTEXT_FIELDS;
};

struct ThreadIdentity {
    uint32_t tid;
    uint32_t generation;
};

enum SchedulerState : uint32_t {
    SCHED_STATE_NONE = 0,
    SCHED_STATE_READY = 1,
    SCHED_STATE_RUNNING = 2,
    SCHED_STATE_WAITING = 3,
    SCHED_STATE_FINISHED = 4,
};

enum ProcessPauseReason : uint32_t {
    PROCESS_PAUSE_NONE = 0,
    PROCESS_PAUSE_YIELD = 1,
    PROCESS_PAUSE_PREEMPT = 2,
    PROCESS_PAUSE_SLEEP = 3,
    PROCESS_PAUSE_WAIT = 4,
};

enum ProcessWaitReason : uint32_t {
    PROCESS_WAIT_NONE = 0,
    PROCESS_WAIT_TIMER = 1,
    PROCESS_WAIT_CHILD = 2,
    PROCESS_WAIT_IPC = 3,
    PROCESS_WAIT_INPUT = 4,
    PROCESS_WAIT_KEY = 5,
    PROCESS_WAIT_CHAR = 6,
    PROCESS_WAIT_THREAD_JOIN = 7,
    PROCESS_WAIT_MUTEX = 8,
    PROCESS_WAIT_SEMAPHORE = 9,
    PROCESS_WAIT_CONDITION = 10,
};

enum ProcessWaitResult : int32_t {
    PROCESS_WAIT_OK = 0,
    PROCESS_WAIT_NOT_READY = -1,
    PROCESS_WAIT_TIMEOUT = -17,
    PROCESS_WAIT_CANCELLED = -18,
};

struct Thread {
    uint32_t tid;
    uint32_t generation;
    uint32_t owner_pid;
    uint32_t owner_generation;
    struct Process* owner;
    ThreadContext* context;
    ThreadContext context_storage;
    uint32_t exit_code;
    uint32_t user_stack_slot;
    uint32_t join_owner_tid;
    uint32_t join_owner_generation;
    uint32_t priority;
    int32_t running_cpu;
    int32_t last_cpu;
    uint32_t affinity_mask;
    uint32_t migration_count;
    uint8_t active;
    uint8_t is_main;
    uint8_t exited;
    uint8_t join_consumed;
    uint8_t runtime_released;
    uint8_t reserved[3];
};

#endif
