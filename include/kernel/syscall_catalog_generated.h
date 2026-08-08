// Generated from config/abi/syscalls.json. Do not edit.
#ifndef KERNEL_SYSCALL_CATALOG_GENERATED_H
#define KERNEL_SYSCALL_CATALOG_GENERATED_H

#include <stdint.h>
#include "os64/syscall_numbers.h"

#define OS64_SYSCALL_FLAG_BLOCKING (1u << 0)
#define OS64_SYSCALL_FLAG_CANCELLABLE (1u << 1)
#define OS64_SYSCALL_FLAG_AUDITED (1u << 2)
#define OS64_SYSCALL_RESULT_STATUS 0u
#define OS64_SYSCALL_RESULT_COUNT 1u
#define OS64_SYSCALL_RESULT_VALUE 2u
#define OS64_SYSCALL_RESULT_HANDLE 3u
#define OS64_SYSCALL_RESULT_ADDRESS 4u
#define OS64_SYSCALL_RESULT_NORETURN 5u
#define OS64_SYSCALL_SUCCESS_ZERO 0u
#define OS64_SYSCALL_SUCCESS_NONNEGATIVE 1u
#define OS64_SYSCALL_SUCCESS_POSITIVE 2u
#define OS64_SYSCALL_SUCCESS_NORETURN 3u
#define OS64_SYSCALL_OUTPUT_NONE 0u
#define OS64_SYSCALL_OUTPUT_ATOMIC 1u
#define OS64_SYSCALL_OUTPUT_PARTIAL 2u
#define OS64_SYSCALL_FAILURE_OUTPUT_NOT_APPLICABLE 0u
#define OS64_SYSCALL_FAILURE_OUTPUT_UNCHANGED 1u
#define OS64_SYSCALL_FAILURE_OUTPUT_PARTIAL 2u
#define OS64_SYSCALL_AUTHORITY_NONE 0u
#define OS64_SYSCALL_AUTHORITY_PERMISSIONS 1u
#define OS64_SYSCALL_AUTHORITY_SUBSYSTEM 2u
#define OS64_SYSCALL_AUTHORITY_PERMISSIONS_THEN_SUBSYSTEM 3u

typedef struct OsSyscallCatalogDescriptor {
    uint32_t number;
    uint8_t argument_count;
    uint8_t pointer_mask;
    uint8_t nullable_mask;
    uint8_t readable_mask;
    uint8_t writable_mask;
    uint8_t snapshot_mask;
    uint8_t nested_mask;
    uint8_t flags;
    uint8_t result_kind;
    uint8_t success_domain;
    uint8_t output_publication;
    uint8_t failure_output;
    uint8_t authority_mode;
    uint8_t reserved0;
    uint16_t argument_alignment[3];
    uint8_t argument_size_register[3];
    uint8_t reserved1;
    uint32_t required_permissions;
    const char* name;
    const char* symbol;
    const char* permission;
    const char* error_set;
} OsSyscallCatalogDescriptor;

static const OsSyscallCatalogDescriptor os64_syscall_catalog[] = {
    {1u, 2u, 1u, 1u, 1u, 0u, 1u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {2u, 0u, 0u}, 0u, 0u, "write", "SYS_WRITE", "none", "console"},
    {2u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 5u, 3u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "exit", "SYS_EXIT", "none", "lifecycle"},
    {3u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "putchar", "SYS_PUTCHAR", "none", "console"},
    {4u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 2u, 1u, 0u, 0u, 2u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "getchar", "SYS_GETCHAR", "OS_PROCESS_PERMISSION_INPUT or attached terminal", "input"},
    {5u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "list_files", "SYS_LIST_FILES", "none", "legacy"},
    {6u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "cat_file", "SYS_CAT_FILE", "none", "legacy"},
    {7u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 64u, "run_user", "SYS_RUN_USER", "OS_PROCESS_PERMISSION_MANAGE_CHILD", "process"},
    {8u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "version", "SYS_VERSION", "none", "legacy"},
    {9u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "bootinfo", "SYS_BOOTINFO", "none", "legacy"},
    {10u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "memstat", "SYS_MEMSTAT", "none", "legacy"},
    {11u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "rm_file", "SYS_RM_FILE", "none", "legacy"},
    {12u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "uptime", "SYS_UPTIME", "none", "legacy"},
    {13u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "touch_file", "SYS_TOUCH_FILE", "none", "legacy"},
    {14u, 2u, 3u, 0u, 3u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "save_file", "SYS_SAVE_FILE", "none", "legacy"},
    {15u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "get_pid", "SYS_GET_PID", "none", "process"},
    {16u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "get_ppid", "SYS_GET_PPID", "none", "process"},
    {17u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "ps", "SYS_PS", "none", "legacy"},
    {18u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "last_status", "SYS_LAST_STATUS", "none", "legacy"},
    {19u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "wait_child", "SYS_WAIT_CHILD", "none", "legacy"},
    {20u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "sched_info", "SYS_SCHED_INFO", "none", "legacy"},
    {21u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "yield", "SYS_YIELD", "none", "none"},
    {22u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 64u, "resume_user", "SYS_RESUME_USER", "OS_PROCESS_PERMISSION_MANAGE_CHILD", "process"},
    {23u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 64u, "kill_user", "SYS_KILL_USER", "OS_PROCESS_PERMISSION_MANAGE_CHILD", "process"},
    {24u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "reap_all_children", "SYS_REAP_ALL_CHILDREN", "none", "legacy"},
    {25u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "jobs", "SYS_JOBS", "none", "legacy"},
    {26u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "sleep", "SYS_SLEEP", "none", "timeout"},
    {27u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 64u, "set_background", "SYS_SET_BACKGROUND", "OS_PROCESS_PERMISSION_MANAGE_CHILD", "process"},
    {28u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "children_active", "SYS_CHILDREN_ACTIVE", "none", "none"},
    {29u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "reap_all_children_silent", "SYS_REAP_ALL_CHILDREN_SILENT", "none", "none"},
    {30u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "rm_file_silent", "SYS_RM_FILE_SILENT", "none", "legacy"},
    {31u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "touch_file_silent", "SYS_TOUCH_FILE_SILENT", "none", "legacy"},
    {32u, 2u, 3u, 0u, 3u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "save_file_silent", "SYS_SAVE_FILE_SILENT", "none", "legacy"},
    {33u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_mounts", "SYS_VFS_MOUNTS", "none", "legacy"},
    {34u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "list_files_at", "SYS_LIST_FILES_AT", "none", "legacy"},
    {35u, 2u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 3u, 2u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_open", "SYS_VFS_OPEN", "none", "vfs"},
    {36u, 3u, 2u, 2u, 0u, 2u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 3u, 0u}, 0u, 0u, "vfs_read", "SYS_VFS_READ", "none", "vfs"},
    {37u, 3u, 2u, 2u, 2u, 0u, 2u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 3u, 0u}, 0u, 0u, "vfs_write", "SYS_VFS_WRITE", "none", "vfs"},
    {38u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_close", "SYS_VFS_CLOSE", "none", "vfs"},
    {39u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_seek", "SYS_VFS_SEEK", "none", "vfs"},
    {40u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_tell", "SYS_VFS_TELL", "none", "vfs"},
    {41u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "mkdir_command", "SYS_MKDIR", "none", "vfs"},
    {42u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "rmdir_command", "SYS_RMDIR", "none", "vfs"},
    {43u, 2u, 3u, 0u, 1u, 2u, 1u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_info", "SYS_VFS_INFO", "none", "vfs"},
    {44u, 2u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {2u, 0u, 0u}, 0u, 0u, "getcwd", "SYS_GETCWD", "none", "vfs"},
    {45u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "chdir", "SYS_CHDIR", "none", "vfs"},
    {46u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 3u, 2u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_opendir", "SYS_VFS_OPENDIR", "none", "vfs"},
    {47u, 2u, 2u, 0u, 0u, 2u, 0u, 0u, 0u, 2u, 1u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_readdir", "SYS_VFS_READDIR", "none", "vfs"},
    {48u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "vfs_closedir", "SYS_VFS_CLOSEDIR", "none", "vfs"},
    {49u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "mkdir", "SYS_MKDIR_SILENT", "none", "vfs"},
    {50u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "rmdir", "SYS_RMDIR_SILENT", "none", "vfs"},
    {51u, 2u, 3u, 0u, 3u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "rename_path", "SYS_RENAME_PATH", "none", "vfs"},
    {52u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "clear_screen", "SYS_CLEAR_SCREEN", "none", "console"},
    {53u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 4u, 2u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "user_brk", "SYS_USER_BRK", "none", "memory"},
    {54u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "time_ticks", "SYS_TIME_TICKS", "none", "none"},
    {55u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "time_frequency", "SYS_TIME_FREQUENCY", "none", "none"},
    {56u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 16u, "gfx_get_info", "SYS_GFX_GET_INFO", "OS_PROCESS_PERMISSION_DISPLAY", "graphics"},
    {57u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 16u, "gfx_put_pixel", "SYS_GFX_PUT_PIXEL", "OS_PROCESS_PERMISSION_DISPLAY", "graphics"},
    {58u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 16u, "gfx_fill_rect", "SYS_GFX_FILL_RECT", "OS_PROCESS_PERMISSION_DISPLAY", "graphics"},
    {59u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 16u, "gfx_clear", "SYS_GFX_CLEAR", "OS_PROCESS_PERMISSION_DISPLAY", "graphics"},
    {60u, 3u, 1u, 0u, 0u, 1u, 0u, 0u, 3u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 8u, "keyboard_event", "SYS_KEYBOARD_EVENT", "OS_PROCESS_PERMISSION_INPUT", "input"},
    {61u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 8u, "input_event_poll", "SYS_INPUT_EVENT_POLL", "OS_PROCESS_PERMISSION_INPUT", "input"},
    {62u, 2u, 1u, 0u, 0u, 1u, 0u, 0u, 3u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 8u, "input_event_wait", "SYS_INPUT_EVENT_WAIT", "OS_PROCESS_PERMISSION_INPUT", "input"},
    {63u, 2u, 2u, 0u, 2u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_send", "SYS_IPC_SEND", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {64u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_recv", "SYS_IPC_RECV", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {65u, 2u, 1u, 0u, 0u, 1u, 0u, 0u, 3u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_wait", "SYS_IPC_WAIT", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {66u, 2u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 2u, "service_register", "SYS_SERVICE_REGISTER", "OS_PROCESS_PERMISSION_SERVICE_REGISTER", "service"},
    {67u, 2u, 3u, 0u, 1u, 2u, 1u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 1u, "service_find", "SYS_SERVICE_FIND", "OS_PROCESS_PERMISSION_SERVICE_DISCOVER", "service"},
    {68u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 2u, "service_unregister", "SYS_SERVICE_UNREGISTER", "OS_PROCESS_PERMISSION_SERVICE_REGISTER", "service"},
    {69u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "get_process_identity", "SYS_GET_PROCESS_IDENTITY", "none", "buffer"},
    {70u, 3u, 4u, 0u, 4u, 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_send_identity", "SYS_IPC_SEND_IDENTITY", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {71u, 2u, 3u, 3u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 2u, 2u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_query", "SYS_IPC_QUERY", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {72u, 3u, 4u, 0u, 4u, 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_v2_send_identity", "SYS_IPC_V2_SEND_IDENTITY", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {73u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_v2_recv", "SYS_IPC_V2_RECV", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {74u, 2u, 3u, 0u, 1u, 2u, 1u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_v2_recv_match", "SYS_IPC_V2_RECV_MATCH", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {75u, 2u, 1u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 64u, "run_user_with_permissions", "SYS_RUN_USER_WITH_PERMISSIONS", "OS_PROCESS_PERMISSION_MANAGE_CHILD", "process"},
    {76u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 2u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 32u, "surface_create", "SYS_SURFACE_CREATE", "OS_PROCESS_PERMISSION_SHARED_SURFACE", "surface"},
    {77u, 2u, 2u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 32u, "surface_get_info", "SYS_SURFACE_GET_INFO", "OS_PROCESS_PERMISSION_SHARED_SURFACE", "surface"},
    {78u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 4u, 2u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 32u, "surface_map", "SYS_SURFACE_MAP", "OS_PROCESS_PERMISSION_SHARED_SURFACE", "surface"},
    {79u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 32u, "surface_unmap", "SYS_SURFACE_UNMAP", "OS_PROCESS_PERMISSION_SHARED_SURFACE", "surface"},
    {80u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 32u, "surface_close", "SYS_SURFACE_CLOSE", "OS_PROCESS_PERMISSION_SHARED_SURFACE", "surface"},
    {81u, 2u, 1u, 0u, 0u, 1u, 0u, 0u, 3u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "ipc_v2_wait", "SYS_IPC_V2_WAIT", "OS_PROCESS_PERMISSION_IPC", "ipc"},
    {82u, 3u, 2u, 0u, 2u, 0u, 2u, 0u, 0u, 1u, 1u, 0u, 0u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 16u, "gfx_present_surface", "SYS_GFX_PRESENT_SURFACE", "OS_PROCESS_PERMISSION_DISPLAY", "graphics"},
    {83u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "handle_close", "SYS_HANDLE_CLOSE", "none", "handle"},
    {84u, 2u, 3u, 0u, 1u, 2u, 1u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 1u, "service_find_owner_identity", "SYS_SERVICE_FIND_OWNER_IDENTITY", "OS_PROCESS_PERMISSION_SERVICE_DISCOVER", "service"},
    {85u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 64u, "process_identity_alive", "SYS_PROCESS_IDENTITY_ALIVE", "OS_PROCESS_PERMISSION_MANAGE_CHILD", "process"},
    {86u, 2u, 2u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 32u, "display_session_acquire", "SYS_DISPLAY_SESSION_ACQUIRE", "OS_PROCESS_PERMISSION_SHARED_SURFACE", "display"},
    {87u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "display_session_release", "SYS_DISPLAY_SESSION_RELEASE", "registered window service owner", "display"},
    {88u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "display_session_get_info", "SYS_DISPLAY_SESSION_GET_INFO", "none", "display"},
    {89u, 2u, 3u, 0u, 1u, 2u, 1u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_create", "SYS_THREAD_CREATE", "none", "thread"},
    {90u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_self", "SYS_THREAD_SELF", "none", "buffer"},
    {91u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 5u, 3u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_exit", "SYS_THREAD_EXIT", "none", "lifecycle"},
    {92u, 3u, 4u, 4u, 0u, 4u, 0u, 0u, 3u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_join", "SYS_THREAD_JOIN", "none", "sync"},
    {93u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 2u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "mutex_create", "SYS_MUTEX_CREATE", "none", "sync"},
    {94u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "mutex_lock", "SYS_MUTEX_LOCK", "none", "sync"},
    {95u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "mutex_unlock", "SYS_MUTEX_UNLOCK", "none", "sync"},
    {96u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 2u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "semaphore_create", "SYS_SEMAPHORE_CREATE", "none", "sync"},
    {97u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "semaphore_wait", "SYS_SEMAPHORE_WAIT", "none", "sync"},
    {98u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "semaphore_post", "SYS_SEMAPHORE_POST", "none", "sync"},
    {99u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 2u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "condition_create", "SYS_CONDITION_CREATE", "none", "sync"},
    {100u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "condition_wait", "SYS_CONDITION_WAIT", "none", "sync"},
    {101u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "condition_signal", "SYS_CONDITION_SIGNAL", "none", "sync"},
    {102u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "condition_broadcast", "SYS_CONDITION_BROADCAST", "none", "sync"},
    {103u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_tls_set", "SYS_THREAD_TLS_SET", "none", "thread"},
    {104u, 1u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_tls_get", "SYS_THREAD_TLS_GET", "none", "buffer"},
    {105u, 3u, 4u, 0u, 0u, 4u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_get_info", "SYS_THREAD_GET_INFO", "none", "thread"},
    {106u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_set_priority", "SYS_THREAD_SET_PRIORITY", "none", "thread"},
    {107u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "thread_set_affinity", "SYS_THREAD_SET_AFFINITY", "none", "thread"},
    {108u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 4u, "terminal_session_bind", "SYS_TERMINAL_SESSION_BIND", "OS_PROCESS_PERMISSION_IPC", "terminal"},
    {109u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "terminal_session_exit", "SYS_TERMINAL_SESSION_EXIT", "none", "terminal"},
    {110u, 3u, 4u, 0u, 0u, 4u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "terminal_session_read", "SYS_TERMINAL_SESSION_READ", "none", "terminal"},
    {111u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {1u, 1u, 1u}, {0u, 0u, 0u}, 0u, 0u, "terminal_session_close", "SYS_TERMINAL_SESSION_CLOSE", "none", "terminal"},
};

#define OS64_SYSCALL_CATALOG_COUNT \
    ((uint32_t)(sizeof(os64_syscall_catalog) / sizeof(os64_syscall_catalog[0])))

#endif
