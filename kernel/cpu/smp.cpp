#include "kernel/smp.h"
#include "kernel/fault_injection.h"

#include "arch/x86_64/apic.h"
#include "arch/x86_64/gdt64.h"
#include "arch/x86_64/idt64.h"
#include "drivers/pit.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"
#include "kernel/process64.h"
#include "kernel/spinlock.h"
#include "kernel/userprog64.h"
#include "kernel/mm/address_space.h"

#ifndef OS64_HOST_TEST
#include "kernel/klog.h"
#include "kernel/kutil64.h"
#include "kernel/mm/vm.h"
#endif

struct __attribute__((packed)) SmpStartupMailbox {
    uint64_t magic;
    uint64_t cr3;
    uint64_t stack_top;
    uint64_t entry;
    uint32_t logical_id;
    uint32_t expected_apic_id;
    uint64_t generation;
};

#define SMP_MAILBOX_MAGIC 0x4F53363441504D42ULL

static SmpStartupStats startup_stats;
static SmpTimeReference time_reference;
static SmpExecutionStats execution_stats;
static volatile uint32_t scheduler_release_generation = 0;
static volatile uint64_t reference_lapic_hz = 0;

#ifndef OS64_HOST_TEST
extern PIT pit;
extern "C" uint8_t _binary_bin_ap_trampoline_bin_start[];
extern "C" uint8_t _binary_bin_ap_trampoline_bin_end[];
extern "C" void smp_ap_entry(uint32_t logical_id);

static volatile SmpStartupMailbox* mailbox() {
    return (volatile SmpStartupMailbox*)(uintptr_t)SMP_MAILBOX_ADDRESS;
}

static uint64_t smp_read_tsc() {
    uint32_t low = 0;
    uint32_t high = 0;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static void cpuid(uint32_t leaf,
                  uint32_t subleaf,
                  uint32_t* eax,
                  uint32_t* ebx,
                  uint32_t* ecx,
                  uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static void detect_time_reference() {
    time_reference = {};
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(0x80000000u, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000007u) {
        cpuid(0x80000007u, 0, &eax, &ebx, &ecx, &edx);
        time_reference.invariant_tsc = (edx & (1u << 8)) != 0;
    }

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    const uint32_t maximum_leaf = eax;
    if (maximum_leaf >= 0x15u) {
        cpuid(0x15u, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0 && ebx != 0 && ecx != 0) {
            time_reference.tsc_hz =
                ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
            time_reference.source = 1;
        }
    }
    if (time_reference.tsc_hz == 0 && maximum_leaf >= 0x16u) {
        cpuid(0x16u, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0) {
            time_reference.tsc_hz = (uint64_t)eax * 1000000ULL;
            time_reference.source = 2;
        }
    }
    if (time_reference.tsc_hz == 0) {
        time_reference.source = 3;
        uint64_t flags = 0;
        __asm__ volatile("pushfq; pop %0" : "=r"(flags));
        const uint64_t initial_tick = pit.get_tick64();
        __asm__ volatile("sti" : : : "memory");
        uint64_t spin_limit = 200000000ULL;
        while (pit.get_tick64() == initial_tick && spin_limit-- != 0) {
            __asm__ volatile("pause");
        }
        const uint64_t start_tick = pit.get_tick64();
        const uint64_t start_tsc = smp_read_tsc();
        const uint64_t target_tick = start_tick + 4u;
        spin_limit = 400000000ULL;
        while (pit.get_tick64() < target_tick && spin_limit-- != 0) {
            __asm__ volatile("pause");
        }
        const uint64_t end_tick = pit.get_tick64();
        const uint64_t end_tsc = smp_read_tsc();
        if ((flags & (1ULL << 9)) == 0) {
            __asm__ volatile("cli" : : : "memory");
        }
        time_reference.pit_epoch = end_tick;
        if (end_tick > start_tick && end_tsc > start_tsc) {
            time_reference.tsc_hz =
                ((end_tsc - start_tsc) * PIT_DEFAULT_HZ) /
                (end_tick - start_tick);
        } else {
            time_reference.tsc_hz = 1000000000ULL;
        }
    }
}

static int configure_local_scheduler_cpu(CpuLocal* local) {
    if (!cpu_local_validate(local) ||
        __atomic_exchange_n(&local->timer_calibration_attempted,
                            1u,
                            __ATOMIC_ACQ_REL)) {
        return __atomic_load_n(&local->scheduler_enabled,
                               __ATOMIC_ACQUIRE) != 0;
    }
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_LOCAL_TIMER)) {
        __atomic_add_fetch(&execution_stats.calibration_failed,
                           1u,
                           __ATOMIC_RELAXED);
        return 0;
    }
    LocalApicTimerCalibration calibration = {};
    if (!interrupt_controller_calibrate_local_timer(
            time_reference.tsc_hz,
            PIT_DEFAULT_HZ,
            SMP_LOCAL_TIMER_VECTOR,
            &calibration)) {
        __atomic_add_fetch(&execution_stats.calibration_failed,
                           1u,
                           __ATOMIC_RELAXED);
        return 0;
    }

    uint64_t expected =
        __atomic_load_n(&reference_lapic_hz, __ATOMIC_ACQUIRE);
    if (local->logical_id == 0 || expected == 0) {
        expected = calibration.timer_hz;
        __atomic_store_n(&reference_lapic_hz, expected, __ATOMIC_RELEASE);
    }
    const uint64_t difference = calibration.timer_hz > expected
        ? calibration.timer_hz - expected : expected - calibration.timer_hz;
    const uint32_t error_bps = expected != 0
        ? (uint32_t)((difference * 10000ULL) / expected) : 10000u;
    local->local_timer_hz = calibration.timer_hz;
    local->local_timer_sample_tsc = calibration.sample_tsc_ticks;
    local->local_timer_reload = calibration.reload;
    local->local_timer_error_bps = error_bps;
    local->local_timer_source = time_reference.source;
    if (error_bps > 1500u) {
        interrupt_controller_stop_local_timer();
        __atomic_add_fetch(&execution_stats.calibration_failed,
                           1u,
                           __ATOMIC_RELAXED);
        return 0;
    }
    local->local_timer_calibrated = 1;
    __atomic_store_n(&local->scheduler_enabled, 1u, __ATOMIC_RELEASE);
    __atomic_add_fetch(&execution_stats.scheduler_cpus,
                       1u,
                       __ATOMIC_RELAXED);
    return 1;
}

static uint64_t startup_timeout_ticks() {
    if (time_reference.tsc_hz != 0) return time_reference.tsc_hz / 2u;
    return 1000000000ULL;
}

static int wait_online(CpuLocal* local) {
    const uint64_t deadline = smp_read_tsc() + startup_timeout_ticks();
    while ((int64_t)(smp_read_tsc() - deadline) < 0) {
        if (__atomic_load_n(&local->online, __ATOMIC_ACQUIRE)) return 1;
        __asm__ volatile("pause");
    }
    return __atomic_load_n(&local->online, __ATOMIC_ACQUIRE) != 0;
}

static int wait_ping(CpuLocal* local, uint64_t previous) {
    const uint64_t deadline = smp_read_tsc() + startup_timeout_ticks();
    while ((int64_t)(smp_read_tsc() - deadline) < 0) {
        if (__atomic_load_n(&local->startup_ping_count, __ATOMIC_ACQUIRE) >
            previous) {
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int install_trampoline() {
    const uint64_t size =
        (uint64_t)(_binary_bin_ap_trampoline_bin_end -
                   _binary_bin_ap_trampoline_bin_start);
    if (size == 0 || size > SMP_MAILBOX_ADDRESS - SMP_TRAMPOLINE_ADDRESS ||
        !vm_map_identity(SMP_TRAMPOLINE_ADDRESS,
                         4096,
                         VM_FLAG_WRITABLE)) {
        return 0;
    }
    volatile uint8_t* destination =
        (volatile uint8_t*)(uintptr_t)SMP_TRAMPOLINE_ADDRESS;
    for (uint64_t i = 0; i < size; i++) {
        destination[i] = _binary_bin_ap_trampoline_bin_start[i];
    }
    return 1;
}

static void publish_mailbox(uint32_t logical_id, uint64_t generation) {
    CpuLocal* local = cpu_local_by_id(logical_id);
    volatile SmpStartupMailbox* box = mailbox();
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    box->magic = 0;
    box->cr3 = cr3;
    box->stack_top = local->kernel_stack_top;
    box->entry = (uint64_t)(uintptr_t)&smp_ap_entry;
    box->logical_id = logical_id;
    box->expected_apic_id = local->apic_id;
    box->generation = generation;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    box->magic = SMP_MAILBOX_MAGIC;
}

extern "C" void smp_ap_entry(uint32_t logical_id) {
    volatile SmpStartupMailbox* box = mailbox();
    if (__atomic_load_n(&box->magic, __ATOMIC_ACQUIRE) != SMP_MAILBOX_MAGIC ||
        box->logical_id != logical_id ||
        !cpu_local_activate(logical_id)) {
        while (1) __asm__ volatile("cli; hlt");
    }
    CpuLocal* local = cpu_local_current();
    if (local == 0 || local->apic_id != box->expected_apic_id) {
        while (1) __asm__ volatile("cli; hlt");
    }

    gdt64_init();
    idt64_load_current();
    if (!interrupt_controller_init_local_cpu() ||
        !cpu_transition(logical_id, CPU_STATE_ONLINE)) {
        while (1) __asm__ volatile("cli; hlt");
    }
    local->online_generation = box->generation;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&local->online, 1u, __ATOMIC_RELEASE);

    while (1) {
        if (__atomic_load_n(&scheduler_release_generation,
                            __ATOMIC_ACQUIRE) != 0 &&
            !local->timer_calibration_attempted) {
            configure_local_scheduler_cpu(local);
        }
        if (__atomic_load_n(&local->scheduler_enabled, __ATOMIC_ACQUIRE)) {
            ThreadIdentity none = {0, 0};
            Thread* thread =
                scheduler_claim_ready_thread(none, 0, 0, 0);
            if (thread != 0) {
                local->scheduler_user_entry_count++;
                scheduler_execute_claimed_thread(thread);
                continue;
            }
        }
        __asm__ volatile("sti; hlt");
        local->idle_wake_count++;
    }
}
#endif

int smp_start_application_processors() {
    startup_stats = {};
    execution_stats = {};
    scheduler_release_generation = 0;
    reference_lapic_hz = 0;
    startup_stats.last_failed_logical_id = CPU_LOGICAL_ID_INVALID;
#ifdef OS64_HOST_TEST
    return 1;
#else
    detect_time_reference();
    const CpuTopologyStats* topology = cpu_topology_stats();
    if (topology == 0 || !topology->topology_valid ||
        topology->record_count <= 1) {
        return topology != 0 && topology->topology_valid;
    }
    if (!install_trampoline() ||
        interrupt_controller_mode() != INTERRUPT_CONTROLLER_APIC) {
        return 0;
    }

    uint64_t generation = 1;
    for (uint32_t logical_id = 1;
         logical_id < topology->record_count;
         logical_id++) {
        CpuLocal* local = cpu_local_by_id(logical_id);
        const CpuRecord* record = cpu_record(logical_id);
        if (local == 0 || record == 0 ||
            kernel_fault_injection_should_fail(
                KERNEL_FAULT_POINT_AP_STARTUP) ||
            !cpu_transition(logical_id, CPU_STATE_STARTING)) {
            startup_stats.failed++;
            startup_stats.last_failed_logical_id = logical_id;
            continue;
        }
        startup_stats.attempted++;
        publish_mailbox(logical_id, generation++);
        if (!interrupt_controller_start_ap(record->apic_id,
                                           SMP_TRAMPOLINE_VECTOR) ||
            !wait_online(local)) {
            cpu_transition(logical_id, CPU_STATE_FAILED);
            startup_stats.failed++;
            startup_stats.last_failed_logical_id = logical_id;
            continue;
        }
        startup_stats.online++;

        for (uint32_t round = 0; round < SMP_STARTUP_PING_ROUNDS; round++) {
            const uint64_t previous =
                __atomic_load_n(&local->startup_ping_count, __ATOMIC_ACQUIRE);
            startup_stats.ping_sent++;
            if (interrupt_controller_send_ipi(record->apic_id,
                                              SMP_STARTUP_PING_VECTOR) &&
                wait_ping(local, previous)) {
                startup_stats.ping_acknowledged++;
            } else {
                startup_stats.failed++;
                startup_stats.last_failed_logical_id = logical_id;
                break;
            }
        }
    }
    mailbox()->magic = 0;
    klog_write(startup_stats.failed == 0 ? KLOG_INFO : KLOG_WARN,
               "smp",
               startup_stats.failed == 0
                   ? "application processors online"
                   : "application processor startup degraded");
    return startup_stats.failed == 0;
#endif
}

int smp_release_scheduler_execution() {
#ifdef OS64_HOST_TEST
    scheduler_release_generation = 1;
    execution_stats.release_generation = 1;
    execution_stats.scheduler_cpus = 1;
    return 1;
#else
    CpuLocal* bsp = cpu_local_current();
    if (!cpu_local_validate(bsp) || bsp->logical_id != 0 ||
        interrupt_controller_mode() != INTERRUPT_CONTROLLER_APIC ||
        !configure_local_scheduler_cpu(bsp)) {
        return 0;
    }
    execution_stats.release_generation = 1;
    __atomic_store_n(&scheduler_release_generation, 1u, __ATOMIC_RELEASE);
    const CpuTopologyStats* topology = cpu_topology_stats();
    for (uint32_t i = 1; topology != 0 && i < topology->record_count; i++) {
        smp_request_reschedule(i);
    }
    const uint64_t deadline = smp_read_tsc() + startup_timeout_ticks();
    while ((int64_t)(smp_read_tsc() - deadline) < 0) {
        uint32_t ready = 0;
        for (uint32_t i = 0; i < topology->record_count; i++) {
            CpuLocal* local = cpu_local_by_id(i);
            if (local != 0 &&
                __atomic_load_n(&local->scheduler_enabled,
                                __ATOMIC_ACQUIRE)) {
                ready++;
            }
        }
        if (ready == topology->online_count) return 1;
        __asm__ volatile("pause");
    }
    return execution_stats.scheduler_cpus != 0;
#endif
}

int smp_scheduler_execution_released() {
    return __atomic_load_n(&scheduler_release_generation,
                           __ATOMIC_ACQUIRE) != 0;
}

int smp_request_reschedule(uint32_t logical_id) {
#ifdef OS64_HOST_TEST
    (void)logical_id;
    return 0;
#else
    CpuLocal* target = cpu_local_by_id(logical_id);
    const CpuRecord* record = cpu_record(logical_id);
    if (target == 0 || record == 0 ||
        record->lifecycle != CPU_STATE_ONLINE ||
        !__atomic_load_n(&target->online, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&target->pending_reschedule,
                                     &expected,
                                     1u,
                                     0,
                                     __ATOMIC_RELEASE,
                                     __ATOMIC_RELAXED)) {
        __atomic_add_fetch(&target->reschedule_coalesced_count,
                           1u,
                           __ATOMIC_RELAXED);
        __atomic_add_fetch(&execution_stats.reschedule_coalesced,
                           1u,
                           __ATOMIC_RELAXED);
        return 1;
    }
    CpuLocal* sender = cpu_local_current();
    if (cpu_local_validate(sender)) sender->reschedule_sent_count++;
    if (!interrupt_controller_send_ipi(record->apic_id,
                                       SMP_RESCHEDULE_VECTOR)) {
        __atomic_store_n(&target->pending_reschedule, 0u, __ATOMIC_RELEASE);
        target->reschedule_ignored_count++;
        return 0;
    }
    __atomic_add_fetch(&execution_stats.reschedule_ipis,
                       1u,
                       __ATOMIC_RELAXED);
    return 1;
#endif
}

void smp_notify_runnable(uint32_t affinity_mask) {
#ifndef OS64_HOST_TEST
    if (!smp_scheduler_execution_released()) return;
    __atomic_add_fetch(&execution_stats.runnable_notifications,
                       1u,
                       __ATOMIC_RELAXED);
    const CpuTopologyStats* topology = cpu_topology_stats();
    if (topology == 0 || topology->record_count == 0) return;
    const uint32_t cursor =
        __atomic_fetch_add(&execution_stats.notify_cursor,
                           1u,
                           __ATOMIC_RELAXED);
    CpuLocal* current = cpu_local_current();
    for (uint32_t offset = 0; offset < topology->record_count; offset++) {
        const uint32_t logical_id =
            (cursor + offset) % topology->record_count;
        CpuLocal* target = cpu_local_by_id(logical_id);
        if ((affinity_mask & (1u << logical_id)) == 0 || target == 0 ||
            !__atomic_load_n(&target->scheduler_enabled, __ATOMIC_ACQUIRE) ||
            (current != 0 && logical_id == current->logical_id)) {
            continue;
        }
        smp_request_reschedule(logical_id);
        return;
    }
#else
    (void)affinity_mask;
#endif
}

int smp_reschedule_handler() {
#ifdef OS64_HOST_TEST
    return 0;
#else
    CpuLocal* local = cpu_local_current();
    if (!cpu_local_validate(local)) {
        interrupt_controller_eoi(0);
        return 0;
    }
    const uint32_t pending =
        __atomic_exchange_n(&local->pending_reschedule,
                            0u,
                            __ATOMIC_ACQ_REL);
    if (pending) local->reschedule_received_count++;
    else local->reschedule_ignored_count++;
    interrupt_controller_eoi(0);
    /*
     * TLB acknowledgement waits deliberately run with interrupts enabled.
     * A reschedule IPI received in that window must be deferred just like a
     * timer tick: scheduler locks are forbidden until the wait phase ends.
     * The interrupted kernel/user return or the next local tick will perform
     * the ordinary ready-thread check.
     */
    if (kernel_in_tlb_wait()) {
        return 0;
    }
    return pending && scheduler_should_reschedule_current();
#endif
}

static void flush_loaded_address_space(CpuLocal* local,
                                       uint64_t identity,
                                       uint64_t root,
                                       uint64_t address,
                                       uint32_t page_count,
                                       int full_flush,
                                       uint64_t generation) {
#ifndef OS64_HOST_TEST
    if (!cpu_local_validate(local) ||
        local->loaded_address_space_identity != identity ||
        local->loaded_address_space_root != root) {
        if (cpu_local_validate(local)) {
            local->tlb_shootdown_stale_count++;
        }
        return;
    }
    if (full_flush || page_count == 0 ||
        page_count > ADDRESS_SPACE_TLB_PAGE_LIMIT) {
        vm_switch_root(root);
        local->tlb_local_flush_count++;
    } else {
        for (uint32_t page = 0; page < page_count; page++) {
            vm_flush_page(address + (uint64_t)page * VM_PAGE_SIZE);
            local->tlb_local_flush_count++;
        }
    }
    __atomic_store_n(&local->observed_tlb_generation,
                     generation,
                     __ATOMIC_RELEASE);
#else
    (void)local;
    (void)identity;
    (void)root;
    (void)address;
    (void)page_count;
    (void)full_flush;
    (void)generation;
#endif
}

int smp_tlb_shootdown(AddressSpace* space,
                      uint64_t address,
                      uint32_t page_count,
                      int full_flush,
                      uint64_t generation,
                      uint64_t operation_token,
                      uint32_t target_mask,
                      uint32_t* acknowledged_mask) {
    if (acknowledged_mask != 0) {
        *acknowledged_mask = 0;
    }
    if (space == 0 || space->root_phys == 0 || generation == 0 ||
        operation_token == 0) {
        return 0;
    }
    if (kernel_fault_injection_should_fail(
            KERNEL_FAULT_POINT_TLB_REQUEST)) {
        return 0;
    }
#ifdef OS64_HOST_TEST
    if (acknowledged_mask != 0) {
        *acknowledged_mask = target_mask;
    }
    return 1;
#else
    CpuLocal* current = cpu_local_current();
    if (!cpu_local_validate(current) || current->interrupt_depth != 0 ||
        kernel_spinlock_depth() != 0) {
        return 0;
    }
    uint64_t saved_flags = 0;
    __asm__ volatile("pushfq; pop %0" : "=r"(saved_flags));
    if ((saved_flags & (1ULL << 9)) == 0) {
        __asm__ volatile("sti" : : : "memory");
    }
    if (!kernel_tlb_wait_enter()) {
        if ((saved_flags & (1ULL << 9)) == 0) {
            __asm__ volatile("cli" : : : "memory");
        }
        return 0;
    }

    uint32_t acknowledged = 0;
    const uint32_t current_bit = 1u << current->logical_id;
    if ((target_mask & current_bit) != 0) {
        flush_loaded_address_space(current,
                                   space->identity,
                                   space->root_phys,
                                   address,
                                   page_count,
                                   full_flush,
                                   generation);
        acknowledged |= current_bit;
    }

    uint32_t published = 0;
    int success = 1;
    const uint64_t deadline = smp_read_tsc() + startup_timeout_ticks();
    const CpuTopologyStats* topology = cpu_topology_stats();
    for (uint32_t logical_id = 0;
         success && topology != 0 && logical_id < topology->record_count;
         logical_id++) {
        const uint32_t bit = 1u << logical_id;
        if ((target_mask & bit) == 0 || bit == current_bit) {
            continue;
        }
        CpuLocal* target = cpu_local_by_id(logical_id);
        const CpuRecord* record = cpu_record(logical_id);
        if (target == 0 || record == 0 ||
            record->lifecycle != CPU_STATE_ONLINE ||
            !__atomic_load_n(&target->online, __ATOMIC_ACQUIRE)) {
            success = 0;
            break;
        }
        while (__atomic_load_n(&target->pending_tlb_shootdown,
                               __ATOMIC_ACQUIRE) != 0) {
            if ((int64_t)(smp_read_tsc() - deadline) >= 0) {
                success = 0;
                break;
            }
            __asm__ volatile("pause");
        }
        if (!success) {
            break;
        }
        target->tlb_request_identity = space->identity;
        target->tlb_request_root = space->root_phys;
        target->tlb_request_generation = generation;
        target->tlb_request_token = operation_token;
        target->tlb_request_address = address;
        target->tlb_request_page_count = page_count;
        target->tlb_request_full = full_flush ? 1u : 0u;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        __atomic_store_n(&target->pending_tlb_shootdown,
                         1u,
                         __ATOMIC_RELEASE);
        current->tlb_shootdown_sent_count++;
        if (!interrupt_controller_send_ipi(record->apic_id,
                                           SMP_TLB_SHOOTDOWN_VECTOR)) {
            __atomic_store_n(&target->pending_tlb_shootdown,
                             0u,
                             __ATOMIC_RELEASE);
            success = 0;
            break;
        }
        published |= bit;
    }

    while (success && (acknowledged & published) != published) {
        for (uint32_t logical_id = 0;
             topology != 0 && logical_id < topology->record_count;
             logical_id++) {
            const uint32_t bit = 1u << logical_id;
            if ((published & bit) == 0 || (acknowledged & bit) != 0) {
                continue;
            }
            CpuLocal* target = cpu_local_by_id(logical_id);
            if (target != 0 &&
                __atomic_load_n(&target->tlb_ack_token,
                                __ATOMIC_ACQUIRE) == operation_token &&
                __atomic_load_n(&target->tlb_ack_generation,
                                __ATOMIC_ACQUIRE) == generation) {
                acknowledged |= bit;
            }
        }
        if ((acknowledged & published) == published) {
            break;
        }
        if ((int64_t)(smp_read_tsc() - deadline) >= 0) {
            success = 0;
            break;
        }
        __asm__ volatile("pause");
    }

    kernel_tlb_wait_leave();
    if ((saved_flags & (1ULL << 9)) == 0) {
        __asm__ volatile("cli" : : : "memory");
    }
    if (acknowledged_mask != 0) {
        *acknowledged_mask = acknowledged;
    }
    return success && (acknowledged & target_mask) == target_mask;
#endif
}

void smp_tlb_shootdown_handler() {
#ifndef OS64_HOST_TEST
    CpuLocal* local = cpu_local_current();
    if (!cpu_local_validate(local)) {
        interrupt_controller_eoi(0);
        return;
    }
    if (__atomic_load_n(&local->pending_tlb_shootdown,
                        __ATOMIC_ACQUIRE) == 0) {
        local->tlb_shootdown_stale_count++;
        interrupt_controller_eoi(0);
        return;
    }

    const uint64_t identity = local->tlb_request_identity;
    const uint64_t root = local->tlb_request_root;
    const uint64_t generation = local->tlb_request_generation;
    const uint64_t token = local->tlb_request_token;
    const uint64_t address = local->tlb_request_address;
    const uint32_t page_count = local->tlb_request_page_count;
    const uint32_t full_flush = local->tlb_request_full;
    local->tlb_shootdown_received_count++;
    flush_loaded_address_space(local,
                               identity,
                               root,
                               address,
                               page_count,
                               full_flush != 0,
                               generation);
    __atomic_store_n(&local->tlb_ack_generation,
                     generation,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&local->tlb_ack_token,
                     token,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&local->pending_tlb_shootdown,
                     0u,
                     __ATOMIC_RELEASE);
    local->tlb_shootdown_ack_count++;
    interrupt_controller_eoi(0);
#endif
}

void smp_startup_ping_handler() {
#ifndef OS64_HOST_TEST
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        __atomic_add_fetch(&local->startup_ping_count, 1u, __ATOMIC_RELEASE);
    }
    interrupt_controller_eoi(0);
#endif
}

int smp_debug_send_nmi(uint32_t logical_id) {
#ifdef OS64_HOST_TEST
    (void)logical_id;
    return 0;
#else
    CpuLocal* target = cpu_local_by_id(logical_id);
    const CpuRecord* record = cpu_record(logical_id);
    CpuLocal* current = cpu_local_current();
    if (target == 0 || record == 0 || current == 0 ||
        logical_id == current->logical_id ||
        record->lifecycle != CPU_STATE_ONLINE ||
        !__atomic_load_n(&target->online, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    const uint64_t previous =
        __atomic_load_n(&target->nmi_count, __ATOMIC_ACQUIRE);
    if (!interrupt_controller_send_nmi(record->apic_id)) return 0;
    const uint64_t deadline = smp_read_tsc() + startup_timeout_ticks();
    while ((int64_t)(smp_read_tsc() - deadline) < 0) {
        if (__atomic_load_n(&target->nmi_count, __ATOMIC_ACQUIRE) > previous) {
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
#endif
}

uint64_t smp_debug_reschedule_burst(uint32_t logical_id, uint32_t count) {
#ifdef OS64_HOST_TEST
    (void)logical_id;
    (void)count;
    return 0;
#else
    CpuLocal* target = cpu_local_by_id(logical_id);
    const CpuRecord* record = cpu_record(logical_id);
    CpuLocal* current = cpu_local_current();
    if (target == 0 || record == 0 || current == 0 ||
        logical_id == current->logical_id || count == 0 ||
        record->lifecycle != CPU_STATE_ONLINE ||
        !__atomic_load_n(&target->scheduler_enabled, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    const uint64_t before =
        __atomic_load_n(&target->reschedule_coalesced_count, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < count; i++) {
        smp_request_reschedule(logical_id);
    }
    const uint64_t after =
        __atomic_load_n(&target->reschedule_coalesced_count, __ATOMIC_ACQUIRE);
    return after - before;
#endif
}

const SmpStartupStats* smp_startup_stats() {
    return &startup_stats;
}

const SmpTimeReference* smp_time_reference() {
    return &time_reference;
}

const SmpExecutionStats* smp_execution_stats() {
    return &execution_stats;
}

void smp_print_summary() {
#ifndef OS64_HOST_TEST
    print("\n=== SMP STARTUP ===");
    print("\nattempted=");
    print_hex32(startup_stats.attempted);
    print(" online=");
    print_hex32(startup_stats.online);
    print(" failed=");
    print_hex32(startup_stats.failed);
    print(" ping_sent=");
    print_hex32(startup_stats.ping_sent);
    print(" ping_ack=");
    print_hex32(startup_stats.ping_acknowledged);
    print("\nlast_failed=");
    print_hex32(startup_stats.last_failed_logical_id);
    print(" invariant_tsc=");
    print_hex32(time_reference.invariant_tsc);
    print(" time_source=");
    print_hex32(time_reference.source);
    print(" tsc_hz=");
    print_hex64(time_reference.tsc_hz);
    print("\nrelease=");
    print_hex32(execution_stats.release_generation);
    print(" scheduler_cpus=");
    print_hex32(execution_stats.scheduler_cpus);
    print(" calibration_failed=");
    print_hex32(execution_stats.calibration_failed);
    print(" notifications=");
    print_hex64(execution_stats.runnable_notifications);
    print(" resched_ipi=");
    print_hex64(execution_stats.reschedule_ipis);
    print(" coalesced=");
    print_hex64(execution_stats.reschedule_coalesced);
    print("\n===================\n");
#endif
}

#ifdef OS64_HOST_TEST
int smp_host_prepare_start(uint32_t logical_id) {
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_AP_STARTUP)) {
        return 0;
    }
    if (!cpu_transition(logical_id, CPU_STATE_STARTING)) return 0;
    startup_stats.attempted++;
    return 1;
}

int smp_host_timeout_start(uint32_t logical_id) {
    if (!cpu_transition(logical_id, CPU_STATE_FAILED)) return 0;
    startup_stats.failed++;
    startup_stats.last_failed_logical_id = logical_id;
    return 1;
}
#endif
