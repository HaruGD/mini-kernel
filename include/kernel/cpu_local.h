#ifndef KERNEL_CPU_LOCAL_H
#define KERNEL_CPU_LOCAL_H

#include <stdint.h>

#include "kernel/cpu.h"

struct Process;
struct Thread;
struct KernelSpinlock;

#define CPU_LOCAL_MAGIC 0x4F5336344350554CULL
#define CPU_LOCAL_EXECUTION_STACK_MAX 32u
#define CPU_LOCAL_LOCK_STACK_MAX 8u
#define CPU_LOCAL_STACK_SIZE 16384u

enum CpuEmergencyKind : uint32_t {
    CPU_EMERGENCY_NMI = 1,
    CPU_EMERGENCY_DOUBLE_FAULT = 2
};

struct CpuLocal {
    CpuLocal* self;
    uint64_t magic;
    uint32_t logical_id;
    uint32_t apic_id;
    uint8_t prepared;
    uint8_t online;
    uint8_t emergency_active;
    uint8_t reserved0;

    Thread* current_thread;
    void* idle_context;
    uint32_t execution_depth;
    uint32_t entry_depth;
    uint32_t interrupt_depth;
    uint32_t preemption_disable_count;
    Process* process_stack[CPU_LOCAL_EXECUTION_STACK_MAX];
    Thread* thread_stack[CPU_LOCAL_EXECUTION_STACK_MAX];
    KernelSpinlock* held_locks[CPU_LOCAL_LOCK_STACK_MAX];
    uint32_t held_lock_depth;
    uint32_t pending_reschedule;

    uint64_t kernel_stack_base;
    uint64_t kernel_stack_top;
    uint64_t nmi_stack_base;
    uint64_t nmi_stack_top;
    uint64_t double_fault_stack_base;
    uint64_t double_fault_stack_top;

    uint64_t nmi_count;
    uint64_t double_fault_count;
    uint64_t emergency_failure_count;
    uint64_t scheduler_tick_count;
    uint64_t timer_interrupt_count;
};

int cpu_local_system_init();
CpuLocal* cpu_local_current();
CpuLocal* cpu_local_by_id(uint32_t logical_id);
int cpu_local_validate(const CpuLocal* local);
CpuLocal* cpu_local_resolve_emergency(uint64_t stack_pointer,
                                     uint32_t emergency_kind);
CpuLocal* cpu_local_emergency_enter(uint64_t stack_pointer,
                                   uint32_t emergency_kind);
void cpu_local_emergency_leave(CpuLocal* local, uint32_t emergency_kind);
void cpu_local_print_summary();

#ifdef OS64_HOST_TEST
void cpu_local_host_select(uint32_t logical_id);
#endif

#endif
