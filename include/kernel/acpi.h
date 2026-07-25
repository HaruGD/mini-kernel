#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

#define ACPI_MAX_IOAPICS 4
#define ACPI_MAX_ISO 16
#define ACPI_MAX_CPUS 8

struct AcpiCpuInfo {
    uint32_t acpi_id;
    uint32_t apic_id;
    uint32_t flags;
    uint8_t entry_type;
    uint8_t enabled;
    uint8_t online_capable;
    uint8_t xapic_supported;
};

struct AcpiIoApicInfo {
    uint8_t id;
    uint32_t address;
    uint32_t gsi_base;
};

struct AcpiInterruptOverride {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
};

struct AcpiState {
    uint8_t ready;
    uint8_t revision;
    uint8_t cpu_parse_valid;
    uint8_t reserved0;
    uint16_t cpu_count;
    uint16_t cpu_total_count;
    uint16_t cpu_disabled_count;
    uint16_t cpu_overflow_count;
    uint16_t cpu_unsupported_count;
    uint16_t cpu_malformed_count;
    uint64_t rsdp_address;
    uint64_t root_table_address;
    uint64_t madt_address;
    uint64_t fadt_address;
    uint64_t local_apic_address;
    uint32_t ioapic_count;
    uint32_t override_count;
    AcpiCpuInfo cpus[ACPI_MAX_CPUS];
    AcpiIoApicInfo ioapics[ACPI_MAX_IOAPICS];
    AcpiInterruptOverride overrides[ACPI_MAX_ISO];
};

int acpi_init(uint64_t rsdp_address);
void acpi_power_reset();
int acpi_power_init(uint64_t fadt_address);
int acpi_power_available();
int acpi_poweroff();
void acpi_power_print();
const AcpiState* acpi_state();
const AcpiInterruptOverride* acpi_find_override(uint8_t source_irq);
void acpi_print_summary();
int acpi_debug_corrupt_rsdp_checksum();
int acpi_debug_corrupt_madt_entry_length();
int acpi_debug_remove_ioapics();

#endif
