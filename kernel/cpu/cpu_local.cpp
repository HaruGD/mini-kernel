#include "kernel/cpu_local.h"

#include <stddef.h>

#ifndef OS64_HOST_TEST
#include "kernel/kutil64.h"
#endif

static CpuLocal cpu_locals[CPU_MAX_COUNT];
alignas(4096) static uint8_t
    kernel_stacks[CPU_MAX_COUNT][CPU_LOCAL_KERNEL_STACK_SIZE];
alignas(4096) static uint8_t
    nmi_stacks[CPU_MAX_COUNT][CPU_LOCAL_EMERGENCY_STACK_SIZE];
alignas(4096) static uint8_t
    double_fault_stacks[CPU_MAX_COUNT][CPU_LOCAL_EMERGENCY_STACK_SIZE];

static_assert(offsetof(CpuLocal, user_state) == CPU_LOCAL_USER_STATE_OFFSET,
              "CpuLocal user-state assembly offset changed");
static_assert(offsetof(CpuLocal, user_state.resume_rflags) == 224,
              "CpuLocal resume-rflags assembly offset changed");

#ifdef OS64_HOST_TEST
static thread_local CpuLocal* selected_local = 0;
#endif

static void clear_local(CpuLocal* local) {
    uint8_t* bytes = (uint8_t*)local;
    for (uint32_t i = 0; i < sizeof(CpuLocal); i++) bytes[i] = 0;
}

static void prepare_local(uint32_t logical_id, const CpuRecord* record) {
    CpuLocal* local = &cpu_locals[logical_id];
    clear_local(local);
    local->self = local;
    local->magic = CPU_LOCAL_MAGIC;
    local->logical_id = logical_id;
    local->apic_id = record->apic_id;
    local->prepared = 1;
    local->online = record->lifecycle == CPU_STATE_ONLINE ? 1u : 0u;
    local->kernel_stack_base =
        (uint64_t)(uintptr_t)&kernel_stacks[logical_id][0];
    local->kernel_stack_top =
        local->kernel_stack_base + CPU_LOCAL_KERNEL_STACK_SIZE;
    local->nmi_stack_base =
        (uint64_t)(uintptr_t)&nmi_stacks[logical_id][0];
    local->nmi_stack_top =
        local->nmi_stack_base + CPU_LOCAL_EMERGENCY_STACK_SIZE;
    local->double_fault_stack_base =
        (uint64_t)(uintptr_t)&double_fault_stacks[logical_id][0];
    local->double_fault_stack_top =
        local->double_fault_stack_base + CPU_LOCAL_EMERGENCY_STACK_SIZE;
}

#ifndef OS64_HOST_TEST
static void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

static void install_kernel_gs(CpuLocal* local) {
    uint64_t cr4 = 0;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~(1ULL << 16);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    write_msr(0xC0000101u, (uint64_t)(uintptr_t)local);
    write_msr(0xC0000102u, (uint64_t)(uintptr_t)local);
}
#endif

int cpu_local_system_init() {
    const CpuTopologyStats* topology = cpu_topology_stats();
    if (topology == 0 || !topology->topology_valid ||
        topology->record_count == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < topology->record_count; i++) {
        const CpuRecord* record = cpu_record(i);
        if (record == 0) return 0;
        prepare_local(i, record);
        if (i != 0 && record->lifecycle == CPU_STATE_DISCOVERED &&
            !cpu_transition(i, CPU_STATE_PREPARED)) {
            return 0;
        }
    }
#ifdef OS64_HOST_TEST
    selected_local = &cpu_locals[0];
#else
    install_kernel_gs(&cpu_locals[0]);
#endif
    return cpu_local_validate(cpu_local_current());
}

int cpu_local_activate(uint32_t logical_id) {
    CpuLocal* local = cpu_local_by_id(logical_id);
    const CpuRecord* record = cpu_record(logical_id);
    if (local == 0 || record == 0 || !cpu_local_validate(local) ||
        local->apic_id != cpu_arch_bootstrap_apic_id()) {
        return 0;
    }
#ifdef OS64_HOST_TEST
    selected_local = local;
#else
    install_kernel_gs(local);
#endif
    return cpu_local_current() == local && cpu_local_validate(local);
}

CpuLocal* cpu_local_current() {
#ifdef OS64_HOST_TEST
    return selected_local;
#else
    CpuLocal* local = 0;
    __asm__ volatile("movq %%gs:0, %0" : "=r"(local));
    return local;
#endif
}

CpuLocal* cpu_local_by_id(uint32_t logical_id) {
    const CpuTopologyStats* topology = cpu_topology_stats();
    if (topology == 0 || logical_id >= topology->record_count) return 0;
    return &cpu_locals[logical_id];
}

int cpu_local_validate(const CpuLocal* local) {
    if (local == 0 || local->self != local || local->magic != CPU_LOCAL_MAGIC ||
        local->logical_id >= CPU_MAX_COUNT || !local->prepared) {
        return 0;
    }
    const CpuRecord* record = cpu_record(local->logical_id);
    return record != 0 && record->apic_id == local->apic_id;
}

CpuLocal* cpu_local_resolve_emergency(uint64_t stack_pointer,
                                     uint32_t emergency_kind) {
    const CpuTopologyStats* topology = cpu_topology_stats();
    if (topology == 0) return 0;
    for (uint32_t i = 0; i < topology->record_count; i++) {
        CpuLocal* local = &cpu_locals[i];
        const uint64_t base = emergency_kind == CPU_EMERGENCY_NMI
            ? local->nmi_stack_base : local->double_fault_stack_base;
        const uint64_t top = emergency_kind == CPU_EMERGENCY_NMI
            ? local->nmi_stack_top : local->double_fault_stack_top;
        if (stack_pointer >= base && stack_pointer < top) {
            return cpu_local_validate(local) ? local : 0;
        }
    }
    return 0;
}

CpuLocal* cpu_local_emergency_enter(uint64_t stack_pointer,
                                   uint32_t emergency_kind) {
    CpuLocal* local = cpu_local_resolve_emergency(stack_pointer, emergency_kind);
    if (local == 0 || local->emergency_active) {
        if (local != 0) local->emergency_failure_count++;
        return 0;
    }
    local->emergency_active = 1;
    local->interrupt_depth++;
    if (emergency_kind == CPU_EMERGENCY_NMI) local->nmi_count++;
    else local->double_fault_count++;
    return local;
}

void cpu_local_emergency_leave(CpuLocal* local, uint32_t emergency_kind) {
    (void)emergency_kind;
    if (local == 0 || !local->emergency_active) return;
    if (local->interrupt_depth != 0) local->interrupt_depth--;
    local->emergency_active = 0;
}

void cpu_local_print_summary() {
#ifndef OS64_HOST_TEST
    const CpuTopologyStats* topology = cpu_topology_stats();
    print("\n=== CPU LOCAL ===");
    for (uint32_t i = 0; topology != 0 && i < topology->record_count; i++) {
        const CpuLocal* local = &cpu_locals[i];
        print("\ncpu[");
        print_hex32(i);
        print("] valid=");
        print_hex32(cpu_local_validate(local));
        print(" prepared=");
        print_hex32(local->prepared);
        print(" online=");
        print_hex32(local->online);
        print(" entry=");
        print_hex32(local->entry_depth);
        print(" irq=");
        print_hex32(local->interrupt_depth);
        print(" preempt=");
        print_hex32(local->preemption_disable_count);
        print(" locks=");
        print_hex32(local->held_lock_depth);
        print(" nmi=");
        print_hex64(local->nmi_count);
        print(" df=");
        print_hex64(local->double_fault_count);
        print(" idle_wake=");
        print_hex64(local->idle_wake_count);
        print(" ping=");
        print_hex64(local->startup_ping_count);
        print(" sched=");
        print_hex32(local->scheduler_enabled);
        print(" timer_ok=");
        print_hex32(local->local_timer_calibrated);
        print(" timer_hz=");
        print_hex64(local->local_timer_hz);
        print(" reload=");
        print_hex32(local->local_timer_reload);
        print(" error_bps=");
        print_hex32(local->local_timer_error_bps);
        print(" local_ticks=");
        print_hex64(local->local_timer_interrupt_count);
        print(" claims=");
        print_hex64(local->scheduler_claim_count);
        print(" user_entries=");
        print_hex64(local->scheduler_user_entry_count);
        print(" rs_sent=");
        print_hex64(local->reschedule_sent_count);
        print(" rs_recv=");
        print_hex64(local->reschedule_received_count);
        print(" rs_coal=");
        print_hex64(local->reschedule_coalesced_count);
        print(" asid=");
        print_hex64(local->loaded_address_space_identity);
        print(" tlb_gen=");
        print_hex64(local->observed_tlb_generation);
        print(" tlb_sent=");
        print_hex64(local->tlb_shootdown_sent_count);
        print(" tlb_recv=");
        print_hex64(local->tlb_shootdown_received_count);
        print(" tlb_ack=");
        print_hex64(local->tlb_shootdown_ack_count);
        print(" tlb_stale=");
        print_hex64(local->tlb_shootdown_stale_count);
        print(" tlb_flush=");
        print_hex64(local->tlb_local_flush_count);
    }
    print("\n=================\n");
#endif
}

#ifdef OS64_HOST_TEST
void cpu_local_host_select(uint32_t logical_id) {
    CpuLocal* local = cpu_local_by_id(logical_id);
    if (local != 0) selected_local = local;
}
#endif
