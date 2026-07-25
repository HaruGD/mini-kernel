#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <stdint.h>

struct AcpiState;

#define CPU_MAX_COUNT 8u
#define CPU_LOGICAL_ID_INVALID 0xFFFFFFFFu

enum CpuLifecycleState : uint8_t {
    CPU_STATE_EMPTY = 0,
    CPU_STATE_DISCOVERED = 1,
    CPU_STATE_PREPARED = 2,
    CPU_STATE_STARTING = 3,
    CPU_STATE_ONLINE = 4,
    CPU_STATE_OFFLINE = 5,
    CPU_STATE_FAILED = 6
};

struct CpuRecord {
    uint32_t logical_id;
    uint32_t apic_id;
    uint32_t acpi_id;
    uint32_t firmware_flags;
    uint8_t lifecycle;
    uint8_t is_bsp;
    uint8_t enabled;
    uint8_t xapic_supported;
};

struct CpuTopologyStats {
    uint32_t record_count;
    uint32_t online_count;
    uint32_t discovered_count;
    uint32_t duplicate_count;
    uint32_t overflow_count;
    uint32_t unsupported_count;
    uint32_t disabled_count;
    uint32_t malformed_count;
    uint32_t bsp_mismatch_count;
    uint32_t topology_valid;
    uint32_t bsp_apic_id;
};

uint32_t cpu_arch_bootstrap_apic_id();
int cpu_topology_init(const AcpiState* acpi, uint32_t bsp_apic_id);
const CpuRecord* cpu_record(uint32_t logical_id);
const CpuRecord* cpu_record_by_apic_id(uint32_t apic_id);
const CpuTopologyStats* cpu_topology_stats();
int cpu_transition_is_legal(uint8_t from, uint8_t to);
int cpu_transition(uint32_t logical_id, uint8_t next);
const char* cpu_lifecycle_name(uint8_t state);
void cpu_print_summary();

#endif
