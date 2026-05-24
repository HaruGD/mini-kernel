#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>

#define PROCESS_NAME_MAX 12

enum ProcessState : uint32_t {
    PROCESS_STATE_EMPTY = 0,
    PROCESS_STATE_LOADED = 1,
    PROCESS_STATE_RUNNING = 2,
    PROCESS_STATE_RETURNED = 3,
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
    uint8_t active;
};

#endif
