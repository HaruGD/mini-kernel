#ifndef OS64_SDK_SYSCALLS_H
#define OS64_SDK_SYSCALLS_H

#include <stdint.h>

struct Process;

bool dispatch_sdk_syscall64(uint64_t syscall_no,
                            uint64_t arg1,
                            uint64_t arg2,
                            uint64_t arg3,
                            uint64_t* result);
void complete_waiting_syscall64(Process* process);

#endif
