#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>

#include "kernel/handle/kernel_handle.h"
#include "kernel/ipc/ipc_mailbox.h"
#include "kernel/input/input_event_queue.h"
#include "kernel/mm/address_space.h"
#include "os64/process_types.h"
#include "os64/terminal_types.h"
#include "kernel/thread.h"

#define PROCESS_NAME_MAX 32
#define PROCESS_ARG_MAX 8
#define PROCESS_CMDLINE_MAX 96
#define PROCESS_SURFACE_MAPPING_MAX 16u
#define PROCESS_TERMINAL_INPUT_CAPACITY 256u
#define PROCESS_TERMINAL_OUTPUT_CAPACITY 256u
#define PROCESS_TERMINAL_OUTPUT_COOKIE 0x544F5554u

struct ProcessSurfaceMapping {
    uint8_t active;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t map_flags;
    uint32_t page_count;
    uint32_t mapping_generation;
    uint64_t object_id;
    uint64_t user_address;
    uint64_t byte_size;
};

typedef struct ProcessIdentity {
    uint32_t pid;
    uint32_t generation;
} ProcessIdentity;

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

enum ShellPromptKind : uint32_t {
    SHELL_PROMPT_NONE = 0,
    SHELL_PROMPT_USH = 1,
    SHELL_PROMPT_CSH = 2,
};

struct Process {
    uint32_t pid;
    uint32_t generation;
    uint32_t parent_pid;
    uint32_t parent_generation;
    char name[PROCESS_NAME_MAX];
    uint64_t code_base;
    uint64_t elf_link_base;
    uint64_t heap_base;
    uint64_t heap_break;
    uint64_t heap_mapped_end;
    uint64_t heap_limit;
    uint64_t entry_point;
    uint32_t image_size;
    uint32_t code_page_count;
    uint32_t elf_alias_page_count;
    uint32_t heap_page_count;
    uint32_t state;
    uint32_t termination_reason;
    uint32_t status_code;
    uint32_t slot_index;
    uint32_t shell_prompt_kind;
    uint32_t argc;
    uint32_t permissions;
    uint8_t active;
    uint8_t reaped;
    uint8_t background;
    uint8_t exiting;
    uint8_t reserved_process;
    uint8_t elf_alias_ready;
    uint32_t thread_count;
    ThreadIdentity main_thread_identity;
    ThreadIdentity fault_thread_identity;
    char cwd[PROCESS_CMDLINE_MAX];
    char command_line[PROCESS_CMDLINE_MAX];
    union {
        ThreadContext main_thread_context;
        struct {
            THREAD_CONTEXT_FIELDS;
        };
    };
    AddressSpace address_space;
    ProcessSurfaceMapping surface_mappings[PROCESS_SURFACE_MAPPING_MAX];
    uint32_t next_surface_mapping_generation;
    uint32_t active_surface_mapping_count;
    KernelHandleTable handle_table;
    KernelInputEventQueue event_queue;
    KernelIpcMailbox ipc_mailbox;
    uint32_t terminal_owner_pid;
    uint32_t terminal_owner_generation;
    uint32_t terminal_peer_pid;
    uint32_t terminal_peer_generation;
    volatile uint32_t terminal_output_sequence;
    uint32_t terminal_last_input_sequence;
    uint32_t terminal_columns;
    uint32_t terminal_rows;
    uint32_t terminal_input_head;
    uint32_t terminal_input_count;
    volatile uint32_t terminal_output_dropped;
    uint8_t terminal_hung_up;
    uint8_t terminal_input[PROCESS_TERMINAL_INPUT_CAPACITY];
    KernelSpinlock terminal_output_lock;
    uint32_t terminal_output_head;
    uint32_t terminal_output_count;
    uint32_t terminal_output_cookie;
    OsTerminalPacket* terminal_output;
};

#undef THREAD_CONTEXT_FIELDS

#endif
