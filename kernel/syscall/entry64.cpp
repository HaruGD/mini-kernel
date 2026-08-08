#include "arch/x86_64/syscall_entry.h"

#include <stddef.h>

#include "arch/x86_64/gdt64.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"
#include "kernel/mm/address_space.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall/user_memory.h"
#include "kernel/thread.h"

#define IA32_EFER  0xC0000080u
#define IA32_STAR  0xC0000081u
#define IA32_LSTAR 0xC0000082u
#define IA32_FMASK 0xC0000084u
#define IA32_EFER_SCE (1ULL << 0)

#define USER_CODE_SELECTOR (GDT64_USER_CODE_SEL | 3u)
#define USER_DATA_SELECTOR (GDT64_USER_DATA_SEL | 3u)
#define USER_RFLAGS_ALLOWED 0x0000000000250FD7ULL
#define USER_RFLAGS_SYSRET_SLOW ((1ULL << 8) | (1ULL << 10) | \
                                 (1ULL << 16) | (1ULL << 18))
#define SYSCALL_RETURN_FAULT_STATUS 0x5E01u

struct SyscallEntryRecord {
    Process* process;
    Thread* thread;
    uint64_t process_identity;
    uint64_t thread_identity;
    uint64_t address_space_identity;
    uint64_t address_space_root;
};

struct SyscallCpuState {
    uint64_t stack_top;
    uint64_t user_rsp;
    uint64_t entry_count;
    uint64_t sysret_count;
    uint64_t iret_count;
    uint64_t abort_count;
    uint32_t fmask;
    uint8_t ready;
    uint8_t user_mode_active;
    uint8_t reserved[2];
};

struct alignas(4096) SyscallCpuStorage {
    SyscallCpuState state;
    SyscallEntryRecord entries[CPU_LOCAL_EXECUTION_STACK_MAX];
    uint32_t depth;
    uint32_t failures;
    uint8_t reserved[2496];
};

static_assert(sizeof(SyscallCpuStorage) == 4096,
              "syscall CPU storage must retain its assembly stride");
static_assert(offsetof(SyscallCpuStorage, state.stack_top) == 0,
              "syscall stack-top assembly offset changed");
static_assert(offsetof(SyscallCpuStorage, state.user_rsp) == 8,
              "syscall user-RSP assembly offset changed");
static_assert(offsetof(SyscallCpuStorage, state.ready) == 52,
              "syscall ready assembly offset changed");
static_assert(offsetof(SyscallCpuStorage, state.user_mode_active) == 53,
              "syscall user marker assembly offset changed");

extern "C" {
alignas(4096) SyscallCpuStorage syscall_cpu_states[CPU_MAX_COUNT] = {};
}

static SyscallCpuStorage* storage_for(const CpuLocal* local) {
    return cpu_local_validate(local) ? &syscall_cpu_states[local->logical_id] : 0;
}

static uint64_t packed_process_identity(const Process* process) {
    return process == 0 ? 0 :
        ((uint64_t)process->generation << 32) | process->pid;
}

static uint64_t packed_thread_identity(const Thread* thread) {
    return thread == 0 ? 0 :
        ((uint64_t)thread->generation << 32) | thread->tid;
}

#ifndef OS64_HOST_TEST
static uint64_t read_msr(uint32_t msr) {
    uint32_t low = 0;
    uint32_t high = 0;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((uint32_t)value),
                       "d"((uint32_t)(value >> 32))
                     : "memory");
}

static int syscall_instruction_supported() {
    uint32_t eax = 0x80000000u;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001u) return 0;
    eax = 0x80000001u;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 11)) != 0;
}
#endif

int syscall_entry_init_current_cpu() {
    CpuLocal* local = cpu_local_current();
    SyscallCpuStorage* storage = storage_for(local);
    if (storage == 0 || storage->state.stack_top == 0 ||
        (storage->state.stack_top & 0xFu) != 0) {
        return 0;
    }
#ifdef OS64_HOST_TEST
    storage->state.fmask = (uint32_t)SYSCALL_ENTRY_FMASK;
    storage->state.ready = 1;
    return 1;
#else
    if (!syscall_instruction_supported()) return 0;

    const uint64_t star =
        ((uint64_t)GDT64_KERNEL64_CODE_SEL << 32) |
        ((uint64_t)(GDT64_USER_DATA_SEL - 8u) << 48);
    write_msr(IA32_EFER, read_msr(IA32_EFER) | IA32_EFER_SCE);
    write_msr(IA32_STAR, star);
    write_msr(IA32_LSTAR, (uint64_t)(uintptr_t)&syscall_entry64);
    write_msr(IA32_FMASK, SYSCALL_ENTRY_FMASK);

    if ((read_msr(IA32_EFER) & IA32_EFER_SCE) == 0 ||
        read_msr(IA32_STAR) != star ||
        read_msr(IA32_LSTAR) != (uint64_t)(uintptr_t)&syscall_entry64 ||
        read_msr(IA32_FMASK) != SYSCALL_ENTRY_FMASK) {
        return 0;
    }
    storage->state.fmask = (uint32_t)SYSCALL_ENTRY_FMASK;
    __atomic_store_n(&storage->state.ready, 1u, __ATOMIC_RELEASE);
    return 1;
#endif
}

int syscall_entry_current_ready() {
    CpuLocal* local = cpu_local_current();
    SyscallCpuStorage* storage = storage_for(local);
    return storage != 0 &&
        __atomic_load_n(&storage->state.ready, __ATOMIC_ACQUIRE);
}

void syscall_entry_set_kernel_stack(uint64_t stack_top) {
    SyscallCpuStorage* storage = storage_for(cpu_local_current());
    if (storage != 0) storage->state.stack_top = stack_top;
}

#ifdef OS64_HOST_TEST
void syscall_entry_host_set_user_active(int active) {
    SyscallCpuStorage* storage = storage_for(cpu_local_current());
    if (storage != 0) storage->state.user_mode_active = active ? 1u : 0u;
}
#endif

static int validate_user_return_frame(const Process* process,
                                      const uint64_t* frame) {
    if (process == 0 || frame == 0 || frame[16] != USER_CODE_SELECTOR ||
        frame[19] != USER_DATA_SELECTOR ||
        !user_address_is_canonical(frame[15]) || frame[15] == 0 ||
        !user_address_is_canonical(frame[18]) || frame[18] == 0 ||
        !address_space_address_has_rights(&process->address_space,
                                          frame[15],
                                          ADDRESS_SPACE_REGION_EXECUTE)) {
        return 0;
    }
    const uint64_t stack_probe = frame[18] - 1u;
    return address_space_address_has_rights(&process->address_space,
                                            stack_probe,
                                            ADDRESS_SPACE_REGION_WRITE);
}

static void fail_current_return(CpuLocal* local, Process* process) {
    SyscallCpuStorage* storage = storage_for(local);
    if (storage != 0) {
        storage->state.abort_count++;
        storage->failures++;
        storage->state.user_mode_active = 0;
    }
    if (process != 0 && process->active) {
        process_mark_failed(process,
                            PROCESS_TERM_GP_FAULT,
                            SYSCALL_RETURN_FAULT_STATUS);
    }
}

extern "C" uint64_t syscall_fast_enter64(uint64_t* frame) {
    CpuLocal* local = cpu_local_current();
    SyscallCpuStorage* storage = storage_for(local);
    Process* process = current_process();
    Thread* thread = current_thread();
    if (storage == 0 || !storage->state.ready ||
        storage->state.user_mode_active != 0 ||
        storage->depth >= CPU_LOCAL_EXECUTION_STACK_MAX ||
        process == 0 || thread == 0 || !process->active || process->exiting ||
        (process->state != PROCESS_STATE_RUNNING &&
         process->state != PROCESS_STATE_PAUSED) || !thread->active ||
        thread->exited || thread->owner != process ||
        local->loaded_address_space != &process->address_space ||
        local->loaded_address_space_identity != process->address_space.identity ||
        local->loaded_address_space_root != process->address_space.root_phys ||
        !validate_user_return_frame(process, frame)) {
        fail_current_return(local, process);
        return 0;
    }

    SyscallEntryRecord* record = &storage->entries[storage->depth++];
    record->process = process;
    record->thread = thread;
    record->process_identity = packed_process_identity(process);
    record->thread_identity = packed_thread_identity(thread);
    record->address_space_identity = process->address_space.identity;
    record->address_space_root = process->address_space.root_phys;
    storage->state.entry_count++;
    return 1;
}

extern "C" uint64_t syscall_fast_return64(uint64_t* frame) {
    CpuLocal* local = cpu_local_current();
    SyscallCpuStorage* storage = storage_for(local);
    Process* process = current_process();
    Thread* thread = current_thread();
    if (storage == 0 || storage->depth == 0) {
        fail_current_return(local, process);
        return SYSCALL_FAST_RETURN_ABORT;
    }
    SyscallEntryRecord record =
        storage->entries[storage->depth - 1u];
    storage->depth--;
    if (process == 0 || thread == 0 || record.process != process ||
        record.thread != thread ||
        record.process_identity != packed_process_identity(process) ||
        record.thread_identity != packed_thread_identity(thread) ||
        record.address_space_identity != process->address_space.identity ||
        record.address_space_root != process->address_space.root_phys ||
        local->loaded_address_space != &process->address_space ||
        local->loaded_address_space_identity != record.address_space_identity ||
        local->loaded_address_space_root != record.address_space_root ||
        !process->active || process->exiting ||
        (process->state != PROCESS_STATE_RUNNING &&
         process->state != PROCESS_STATE_PAUSED) || !thread->active ||
        thread->exited || !validate_user_return_frame(process, frame)) {
        fail_current_return(local, process);
        return SYSCALL_FAST_RETURN_ABORT;
    }

    const uint64_t original_flags = frame[17];
    frame[17] = (original_flags & USER_RFLAGS_ALLOWED) | (1ULL << 1);
    storage->state.user_mode_active = 1;
    if ((original_flags & ~USER_RFLAGS_ALLOWED) != 0 ||
        (original_flags & USER_RFLAGS_SYSRET_SLOW) != 0) {
        storage->state.iret_count++;
        return SYSCALL_FAST_RETURN_IRET;
    }
    storage->state.sysret_count++;
    return SYSCALL_FAST_RETURN_SYSRET;
}

extern "C" void syscall_fast_leave_to_kernel64() {
    CpuLocal* local = cpu_local_current();
    SyscallCpuStorage* storage = storage_for(local);
    if (storage == 0) return;
    if (storage->depth != 0) {
        storage->depth--;
    } else {
        storage->failures++;
    }
    storage->state.user_mode_active = 0;
}

void syscall_entry_get_diagnostics(SyscallEntryDiagnostics* output) {
    if (output == 0) return;
    *output = {};
    const CpuTopologyStats* topology = cpu_topology_stats();
    for (uint32_t i = 0; topology != 0 && i < topology->record_count; i++) {
        const CpuLocal* local = cpu_local_by_id(i);
        if (!cpu_local_validate(local)) continue;
        const SyscallCpuStorage* storage = &syscall_cpu_states[i];
        output->cpu_count++;
        output->ready_cpu_count += storage->state.ready ? 1u : 0u;
        output->entry_count += storage->state.entry_count;
        output->sysret_count += storage->state.sysret_count;
        output->iret_count += storage->state.iret_count;
        output->abort_count += storage->state.abort_count;
        output->entry_failures += storage->failures;
        output->fmask = storage->state.fmask;
    }
}
