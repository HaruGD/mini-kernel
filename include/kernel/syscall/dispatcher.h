#ifndef KERNEL_SYSCALL_DISPATCHER_H
#define KERNEL_SYSCALL_DISPATCHER_H

#include <stdint.h>

#include "kernel/syscall_catalog_generated.h"
#include "os64/result.h"

struct Process;
struct Thread;

enum SyscallRejectReason : uint32_t {
    SYSCALL_REJECT_NONE = 0,
    SYSCALL_REJECT_UNKNOWN_NUMBER = 1,
    SYSCALL_REJECT_NO_PROCESS_CONTEXT = 2,
    SYSCALL_REJECT_PROCESS_LIFECYCLE = 3,
    SYSCALL_REJECT_NO_THREAD_CONTEXT = 4,
    SYSCALL_REJECT_THREAD_OWNER = 5,
    SYSCALL_REJECT_PERMISSION_STATE = 6,
    SYSCALL_REJECT_PERMISSION_DENIED = 7,
    SYSCALL_REJECT_NULL_POINTER = 8,
    SYSCALL_REJECT_POINTER_FORMAT = 9,
    SYSCALL_REJECT_POINTER_ACCESS = 10,
    SYSCALL_REJECT_EXECUTION_CONTEXT = 11,
    SYSCALL_REJECT_REASON_COUNT = 12
};

struct SyscallDispatchDiagnostics {
    uint64_t total_calls;
    uint64_t dispatched_calls;
    uint64_t rejected_calls;
    uint64_t rejected_by_reason[SYSCALL_REJECT_REASON_COUNT];
    uint64_t last_rejected_number;
    uint32_t last_rejected_reason;
    uint32_t reserved0;
};

const OsSyscallCatalogDescriptor* syscall_descriptor_lookup(uint64_t number);
OsResult syscall_dispatch_preflight64(
    const OsSyscallCatalogDescriptor* descriptor,
    Process* process,
    Thread* thread,
    const uint64_t arguments[3],
    uint32_t* reject_reason_out);
void syscall_dispatch_get_diagnostics(SyscallDispatchDiagnostics* output);
void syscall_dispatch_reset_diagnostics();
const char* syscall_reject_reason_name(uint32_t reason);

uint64_t syscall_dispatch_handler64(uint64_t syscall_no,
                                    uint64_t arg1,
                                    uint64_t arg2,
                                    uint64_t arg3);

#endif
