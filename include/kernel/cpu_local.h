#ifndef KERNEL_CPU_LOCAL_H
#define KERNEL_CPU_LOCAL_H

#include <stdint.h>

#include "kernel/cpu.h"

struct Process;
struct Thread;
struct KernelSpinlock;
struct AddressSpace;

#define CPU_LOCAL_MAGIC 0x4F5336344350554CULL
#define CPU_LOCAL_EXECUTION_STACK_MAX 32u
#define CPU_LOCAL_LOCK_STACK_MAX 8u
#define CPU_LOCAL_KERNEL_STACK_SIZE 65536u
#define CPU_LOCAL_EMERGENCY_STACK_SIZE 16384u
#define CPU_LOCAL_USER_STATE_OFFSET 32u

struct CpuUserState {
    uint64_t return_rsp;
    uint64_t saved_rbx;
    uint64_t saved_rbp;
    uint64_t saved_r12;
    uint64_t saved_r13;
    uint64_t saved_r14;
    uint64_t saved_r15;
    uint64_t resume_rax;
    uint64_t resume_rbx;
    uint64_t resume_rcx;
    uint64_t resume_rdx;
    uint64_t resume_rbp;
    uint64_t resume_rsi;
    uint64_t resume_rdi;
    uint64_t resume_r8;
    uint64_t resume_r9;
    uint64_t resume_r10;
    uint64_t resume_r11;
    uint64_t resume_r12;
    uint64_t resume_r13;
    uint64_t resume_r14;
    uint64_t resume_r15;
    uint64_t resume_rip;
    uint64_t resume_rsp;
    uint64_t resume_rflags;
    uint32_t return_reason;
    uint32_t reserved;
};

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

    CpuUserState user_state;
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
    uint32_t tlb_wait_depth;
    uint32_t pending_tlb_shootdown;

    AddressSpace* loaded_address_space;
    uint64_t loaded_address_space_identity;
    uint64_t loaded_address_space_root;
    uint64_t observed_tlb_generation;
    uint64_t tlb_request_identity;
    uint64_t tlb_request_root;
    uint64_t tlb_request_generation;
    uint64_t tlb_request_token;
    uint64_t tlb_request_address;
    uint32_t tlb_request_page_count;
    uint32_t tlb_request_full;
    uint64_t tlb_ack_generation;
    uint64_t tlb_ack_token;

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
    uint64_t idle_wake_count;
    uint64_t startup_ping_count;
    uint64_t online_generation;
    uint64_t local_timer_hz;
    uint64_t local_timer_sample_tsc;
    uint32_t local_timer_reload;
    uint32_t local_timer_error_bps;
    uint32_t local_timer_source;
    uint8_t local_timer_calibrated;
    uint8_t timer_calibration_attempted;
    uint8_t scheduler_enabled;
    uint8_t reserved1;
    uint64_t local_timer_interrupt_count;
    uint64_t scheduler_claim_count;
    uint64_t scheduler_user_entry_count;
    uint64_t reschedule_sent_count;
    uint64_t reschedule_received_count;
    uint64_t reschedule_coalesced_count;
    uint64_t reschedule_ignored_count;
    uint64_t tlb_shootdown_sent_count;
    uint64_t tlb_shootdown_received_count;
    uint64_t tlb_shootdown_ack_count;
    uint64_t tlb_shootdown_stale_count;
    uint64_t tlb_local_flush_count;
};

int cpu_local_system_init();
int cpu_local_activate(uint32_t logical_id);
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
