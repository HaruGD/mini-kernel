#ifndef KERNEL_ACPI_MADT_H
#define KERNEL_ACPI_MADT_H

#include <stdint.h>

struct AcpiState;

int acpi_madt_parse_cpu_entries(const uint8_t* entries,
                                uint32_t length,
                                AcpiState* state);

#endif
