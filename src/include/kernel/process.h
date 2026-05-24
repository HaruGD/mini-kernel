#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>

#define PROCESS_NAME_MAX 12

enum ProcessState : uint32_t {
    PROCESS_STATE_EMPTY = 0,
    PROCESS_STATE_LOADED = 1,
    PROCESS_STATE_RUNNING = 2,
    PROCESS_STATE_RETURNED = 3,
    PROCESS_STATE_FAILED = 4,
};

enum ProcessTerminationReason : uint32_t {
    PROCESS_TERM_NONE = 0,
    PROCESS_TERM_EXIT = 1,
    PROCESS_TERM_LOAD_ERROR = 2,
    PROCESS_TERM_READ_ERROR = 3,
    PROCESS_TERM_MEMORY_ERROR = 4,
    PROCESS_TERM_MAP_ERROR = 5,
    PROCESS_TERM_PAGE_FAULT = 6,
    PROCESS_TERM_GP_FAULT = 7,
    PROCESS_TERM_DOUBLE_FAULT = 8,
};

enum SchedulerState : uint32_t {
    SCHED_STATE_NONE = 0,
    SCHED_STATE_READY = 1,
    SCHED_STATE_RUNNING = 2,
    SCHED_STATE_WAITING = 3,
    SCHED_STATE_FINISHED = 4,
};

struct Process {
    uint32_t pid;
    uint32_t parent_pid;
    char name[PROCESS_NAME_MAX];
    uint64_t code_base;
    uint64_t stack_base;
    uint64_t entry_point;
    uint32_t image_size;
    uint32_t state;
    uint32_t termination_reason;
    uint32_t status_code;
    uint32_t scheduler_state;
    uint32_t runtime_ticks;
    uint32_t timeslice_ticks;
    uint8_t active;
    uint8_t reaped;
};

#endif
