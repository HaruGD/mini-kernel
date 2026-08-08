#ifndef ARCH_X86_64_SYSCALL_ENTRY_H
#define ARCH_X86_64_SYSCALL_ENTRY_H

#include <stdint.h>

#define SYSCALL_ENTRY_TRANSPORT_INT80 0u
#define SYSCALL_ENTRY_TRANSPORT_FAST  1u

#define SYSCALL_FAST_RETURN_ABORT  0u
#define SYSCALL_FAST_RETURN_SYSRET 1u
#define SYSCALL_FAST_RETURN_IRET   2u

#define SYSCALL_ENTRY_FMASK 0x0000000000044700ULL

struct SyscallEntryDiagnostics {
    uint32_t cpu_count;
    uint32_t ready_cpu_count;
    uint64_t entry_count;
    uint64_t sysret_count;
    uint64_t iret_count;
    uint64_t abort_count;
    uint64_t entry_failures;
    uint32_t fmask;
    uint32_t reserved;
};

int syscall_entry_init_current_cpu();
int syscall_entry_current_ready();
void syscall_entry_set_kernel_stack(uint64_t stack_top);
void syscall_entry_get_diagnostics(SyscallEntryDiagnostics* output);

#ifdef OS64_HOST_TEST
void syscall_entry_host_set_user_active(int active);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void syscall_entry64();
uint64_t syscall_fast_enter64(uint64_t* frame);
uint64_t syscall_fast_return64(uint64_t* frame);
void syscall_fast_leave_to_kernel64();

#ifdef __cplusplus
}
#endif

#endif
