#ifndef KERNEL_SYSCALL64_H
#define KERNEL_SYSCALL64_H

#include <stdint.h>

#include "drivers/keyboard.h"
#include "drivers/pit.h"

#define SYSCALL_RETURN_TO_KERNEL 0xFFFFFFFFFFFFFFFEULL
#define SYSCALL_YIELD_TO_KERNEL  0xFFFFFFFFFFFFFFFDULL
#define TIMER_PREEMPT_TO_KERNEL  0xFFFFFFFFFFFFFFFCULL
#define SYSCALL_SLEEP_TO_KERNEL  0xFFFFFFFFFFFFFFFBULL

#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_PUTCHAR 3
#define SYS_GETCHAR 4
#define SYS_LIST_FILES 5
#define SYS_CAT_FILE 6
#define SYS_RUN_USER 7
#define SYS_VERSION 8
#define SYS_BOOTINFO 9
#define SYS_MEMSTAT 10
#define SYS_RM_FILE 11
#define SYS_UPTIME 12
#define SYS_TOUCH_FILE 13
#define SYS_SAVE_FILE 14
#define SYS_GET_PID 15
#define SYS_GET_PPID 16
#define SYS_PS 17
#define SYS_LAST_STATUS 18
#define SYS_WAIT_CHILD 19
#define SYS_SCHED_INFO 20
#define SYS_YIELD 21
#define SYS_RESUME_USER 22
#define SYS_KILL_USER 23
#define SYS_REAP_ALL_CHILDREN 24
#define SYS_JOBS 25
#define SYS_SLEEP 26
#define SYS_SET_BACKGROUND 27
#define SYS_CHILDREN_ACTIVE 28
#define SYS_REAP_ALL_CHILDREN_SILENT 29
#define SYS_RM_FILE_SILENT 30
#define SYS_TOUCH_FILE_SILENT 31
#define SYS_SAVE_FILE_SILENT 32
#define SYS_VFS_MOUNTS 33
#define SYS_LIST_FILES_AT 34
#define SYS_VFS_OPEN 35
#define SYS_VFS_READ 36
#define SYS_VFS_WRITE 37
#define SYS_VFS_CLOSE 38
#define SYS_VFS_SEEK 39
#define SYS_VFS_TELL 40
#define SYS_MKDIR 41
#define SYS_RMDIR 42
#define SYS_VFS_INFO 43
#define SYS_GETCWD 44
#define SYS_CHDIR 45
#define SYS_VFS_OPENDIR 46
#define SYS_VFS_READDIR 47
#define SYS_VFS_CLOSEDIR 48
#define SYS_MKDIR_SILENT 49
#define SYS_RMDIR_SILENT 50
#define SYS_RENAME_PATH 51
#define SYS_CLEAR_SCREEN 52
#define SYS_USER_BRK 53

extern KeyboardDriver keyboard;
extern PIT pit;

void redraw_user_shell_prompt_if_needed();
int continue_woken_processes(uint32_t exclude_pid);
int continue_background_processes(uint32_t exclude_pid);
int run_user_program(const char* command_line);
void print_boot_info();
void command_version();
void command_memstat();
void command_uptime();
int resume_user_program(uint32_t pid);
int kill_user_program(uint32_t pid);
int set_user_program_background(uint32_t pid, uint32_t enabled);

uint32_t kernel_syscall_count();

extern "C" void process_record_fault64(uint32_t reason, uint32_t status_code);
extern "C" uint64_t process_fault_returnable64();
extern "C" uint64_t syscall_dispatch64(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#endif
