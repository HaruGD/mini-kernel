#include "kernel/smp.h"

#include "arch/x86_64/apic.h"
#include "arch/x86_64/gdt64.h"
#include "arch/x86_64/idt64.h"
#include "kernel/cpu.h"
#include "kernel/cpu_local.h"

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

#ifndef OS64_HOST_TEST
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
        time_reference.pit_epoch = 0;
    }
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
        __asm__ volatile("sti; hlt");
        local->idle_wake_count++;
    }
}
#endif

int smp_start_application_processors() {
    startup_stats = {};
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

const SmpStartupStats* smp_startup_stats() {
    return &startup_stats;
}

const SmpTimeReference* smp_time_reference() {
    return &time_reference;
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
    print("\n===================\n");
#endif
}

#ifdef OS64_HOST_TEST
int smp_host_prepare_start(uint32_t logical_id) {
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
