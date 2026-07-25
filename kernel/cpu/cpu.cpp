#include "kernel/cpu.h"

#include "kernel/acpi.h"
#ifndef OS64_HOST_TEST
#include "kernel/kutil64.h"
#endif

static CpuRecord records[CPU_MAX_COUNT];
static CpuTopologyStats stats;

static void clear_topology() {
    stats = {};
    for (uint32_t i = 0; i < CPU_MAX_COUNT; i++) {
        records[i] = {};
        records[i].logical_id = CPU_LOGICAL_ID_INVALID;
    }
}

uint32_t cpu_arch_bootstrap_apic_id() {
#ifdef OS64_HOST_TEST
    return 0;
#else
    uint32_t eax = 1;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (ebx >> 24) & 0xFFu;
#endif
}

static int apic_already_present(uint32_t apic_id) {
    for (uint32_t i = 0; i < stats.record_count; i++) {
        if (records[i].apic_id == apic_id) {
            return 1;
        }
    }
    return 0;
}

static void append_record(const AcpiCpuInfo* source,
                          uint8_t is_bsp,
                          uint8_t lifecycle) {
    CpuRecord* record = &records[stats.record_count];
    record->logical_id = stats.record_count;
    record->apic_id = source->apic_id;
    record->acpi_id = source->acpi_id;
    record->firmware_flags = source->flags;
    record->lifecycle = lifecycle;
    record->is_bsp = is_bsp;
    record->enabled = source->enabled;
    record->xapic_supported = source->xapic_supported;
    stats.record_count++;
    if (lifecycle == CPU_STATE_ONLINE) {
        stats.online_count++;
    } else if (lifecycle == CPU_STATE_DISCOVERED) {
        stats.discovered_count++;
    }
}

int cpu_topology_init(const AcpiState* acpi, uint32_t bsp_apic_id) {
    clear_topology();
    stats.bsp_apic_id = bsp_apic_id;
    if (acpi == 0 || !acpi->cpu_parse_valid || acpi->cpu_count == 0) {
        stats.malformed_count = acpi != 0 ? acpi->cpu_malformed_count : 1u;
        return 0;
    }
    stats.overflow_count = acpi->cpu_overflow_count;
    stats.unsupported_count = acpi->cpu_unsupported_count;
    stats.disabled_count = acpi->cpu_disabled_count;
    stats.malformed_count = acpi->cpu_malformed_count;

    const AcpiCpuInfo* bsp = 0;
    for (uint32_t i = 0; i < acpi->cpu_count; i++) {
        const AcpiCpuInfo* candidate = &acpi->cpus[i];
        if (candidate->xapic_supported && candidate->apic_id == bsp_apic_id) {
            if (bsp == 0) bsp = candidate;
        }
    }
    if (bsp == 0) {
        stats.bsp_mismatch_count = 1;
        return 0;
    }
    append_record(bsp, 1, CPU_STATE_ONLINE);

    for (uint32_t i = 0; i < acpi->cpu_count; i++) {
        const AcpiCpuInfo* candidate = &acpi->cpus[i];
        if (candidate == bsp) {
            continue;
        }
        if (!candidate->xapic_supported) {
            continue;
        }
        if (apic_already_present(candidate->apic_id)) {
            stats.duplicate_count++;
            continue;
        }
        if (stats.record_count >= CPU_MAX_COUNT) {
            stats.overflow_count++;
            continue;
        }
        append_record(candidate, 0, CPU_STATE_DISCOVERED);
    }
    stats.topology_valid =
        stats.duplicate_count == 0 &&
        stats.overflow_count == 0 &&
        stats.malformed_count == 0;
    return stats.topology_valid;
}

const CpuRecord* cpu_record(uint32_t logical_id) {
    return logical_id < stats.record_count ? &records[logical_id] : 0;
}

const CpuRecord* cpu_record_by_apic_id(uint32_t apic_id) {
    for (uint32_t i = 0; i < stats.record_count; i++) {
        if (records[i].apic_id == apic_id) {
            return &records[i];
        }
    }
    return 0;
}

const CpuTopologyStats* cpu_topology_stats() {
    return &stats;
}

int cpu_transition_is_legal(uint8_t from, uint8_t to) {
    if (from == CPU_STATE_DISCOVERED && to == CPU_STATE_PREPARED) return 1;
    if (from == CPU_STATE_PREPARED && to == CPU_STATE_STARTING) return 1;
    if (from == CPU_STATE_STARTING &&
        (to == CPU_STATE_ONLINE || to == CPU_STATE_FAILED)) return 1;
    if (from == CPU_STATE_ONLINE && to == CPU_STATE_OFFLINE) return 1;
    return 0;
}

int cpu_transition(uint32_t logical_id, uint8_t next) {
    if (logical_id >= stats.record_count ||
        !cpu_transition_is_legal(records[logical_id].lifecycle, next)) {
        return 0;
    }
    const uint8_t previous = records[logical_id].lifecycle;
    records[logical_id].lifecycle = next;
    if (previous == CPU_STATE_DISCOVERED) stats.discovered_count--;
    if (previous == CPU_STATE_ONLINE) stats.online_count--;
    if (next == CPU_STATE_DISCOVERED) stats.discovered_count++;
    if (next == CPU_STATE_ONLINE) stats.online_count++;
    return 1;
}

const char* cpu_lifecycle_name(uint8_t state) {
    if (state == CPU_STATE_DISCOVERED) return "discovered";
    if (state == CPU_STATE_PREPARED) return "prepared";
    if (state == CPU_STATE_STARTING) return "starting";
    if (state == CPU_STATE_ONLINE) return "online";
    if (state == CPU_STATE_OFFLINE) return "offline";
    if (state == CPU_STATE_FAILED) return "failed";
    return "empty";
}

void cpu_print_summary() {
#ifndef OS64_HOST_TEST
    print("\n=== CPU TOPOLOGY ===\nrecords=");
    print_hex32(stats.record_count);
    print(" online=");
    print_hex32(stats.online_count);
    print(" discovered=");
    print_hex32(stats.discovered_count);
    print(" capacity=");
    print_hex32(CPU_MAX_COUNT);
    print("\nvalid=");
    print_hex32(stats.topology_valid);
    print(" bsp_apic=");
    print_hex32(stats.bsp_apic_id);
    print(" duplicate=");
    print_hex32(stats.duplicate_count);
    print(" overflow=");
    print_hex32(stats.overflow_count);
    print(" unsupported=");
    print_hex32(stats.unsupported_count);
    print(" disabled=");
    print_hex32(stats.disabled_count);
    for (uint32_t i = 0; i < stats.record_count; i++) {
        print("\ncpu[");
        print_hex32(i);
        print("] logical=");
        print_hex32(records[i].logical_id);
        print(" apic=");
        print_hex32(records[i].apic_id);
        print(" acpi=");
        print_hex32(records[i].acpi_id);
        print(" state=");
        print(cpu_lifecycle_name(records[i].lifecycle));
        print(" bsp=");
        print_hex32(records[i].is_bsp);
    }
    print("\n====================\n");
#endif
}
