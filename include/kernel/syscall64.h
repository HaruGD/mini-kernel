#ifndef KERNEL_SYSCALL64_H
#define KERNEL_SYSCALL64_H

#include <stdint.h>

#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "os64/result.h"
#include "os64/syscall_numbers.h"

#define SYSCALL_RETURN_TO_KERNEL 0xFFFFFFFF80005301ULL
#define SYSCALL_YIELD_TO_KERNEL  0xFFFFFFFF80005302ULL
#define TIMER_PREEMPT_TO_KERNEL  0xFFFFFFFF80005303ULL
#define SYSCALL_SLEEP_TO_KERNEL  0xFFFFFFFF80005304ULL
#define SYSCALL_WAIT_TO_KERNEL   0xFFFFFFFF80005305ULL

extern KeyboardDriver keyboard;
extern PIT pit;

void redraw_user_shell_prompt_if_needed();
int continue_ready_processes(uint32_t exclude_pid);
int continue_woken_processes(uint32_t exclude_pid);
int continue_background_processes(uint32_t exclude_pid);
int run_user_program(const char* command_line);
int run_user_program_with_permissions(const char* command_line, uint32_t permissions);
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
