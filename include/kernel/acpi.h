#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

#define ACPI_MAX_IOAPICS 4
#define ACPI_MAX_ISO 16

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
    uint16_t cpu_count;
    uint64_t rsdp_address;
    uint64_t root_table_address;
    uint64_t madt_address;
    uint64_t local_apic_address;
    uint32_t ioapic_count;
    uint32_t override_count;
    AcpiIoApicInfo ioapics[ACPI_MAX_IOAPICS];
    AcpiInterruptOverride overrides[ACPI_MAX_ISO];
};

int acpi_init(uint64_t rsdp_address);
const AcpiState* acpi_state();
const AcpiInterruptOverride* acpi_find_override(uint8_t source_irq);
void acpi_print_summary();

#endif
