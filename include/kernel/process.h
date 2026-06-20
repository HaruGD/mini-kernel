#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>

#define PROCESS_NAME_MAX 32
#define PROCESS_ARG_MAX 8
#define PROCESS_CMDLINE_MAX 96

enum ProcessState : uint32_t {
    PROCESS_STATE_EMPTY = 0,
    PROCESS_STATE_LOADED = 1,
    PROCESS_STATE_RUNNING = 2,
    PROCESS_STATE_RETURNED = 3,
    PROCESS_STATE_FAILED = 4,
    PROCESS_STATE_PAUSED = 5,
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
    PROCESS_TERM_KILLED = 9,
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
};

enum ShellPromptKind : uint32_t {
    SHELL_PROMPT_NONE = 0,
    SHELL_PROMPT_USH = 1,
    SHELL_PROMPT_CSH = 2,
};

struct Process {
    uint32_t pid;
    uint32_t parent_pid;
    char name[PROCESS_NAME_MAX];
    uint64_t code_base;
    uint64_t stack_base;
    uint64_t heap_base;
    uint64_t heap_break;
    uint64_t heap_mapped_end;
    uint64_t heap_limit;
    uint64_t entry_point;
    uint32_t image_size;
    uint32_t code_page_count;
    uint32_t stack_page_count;
    uint32_t heap_page_count;
    uint32_t state;
    uint32_t termination_reason;
    uint32_t status_code;
    uint32_t scheduler_state;
    uint32_t runtime_ticks;
    uint32_t timeslice_ticks;
    uint32_t slot_index;
    uint32_t shell_prompt_kind;
    uint32_t argc;
    uint8_t active;
    uint8_t reaped;
    uint8_t resumable;
    uint8_t background;
    uint8_t pause_reason;
    uint32_t wake_tick;
    char cwd[PROCESS_CMDLINE_MAX];
    char command_line[PROCESS_CMDLINE_MAX];
    uint64_t saved_rax;
    uint64_t saved_rbx;
    uint64_t saved_rcx;
    uint64_t saved_rdx;
    uint64_t saved_rbp;
    uint64_t saved_rsi;
    uint64_t saved_rdi;
    uint64_t saved_r8;
    uint64_t saved_r9;
    uint64_t saved_r10;
    uint64_t saved_r11;
    uint64_t saved_r12;
    uint64_t saved_r13;
    uint64_t saved_r14;
    uint64_t saved_r15;
    uint64_t saved_rip;
    uint64_t saved_rsp;
    uint64_t saved_rflags;
};

#endif
