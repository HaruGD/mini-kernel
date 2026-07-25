#include "kernel/acpi.h"
#include "kernel/acpi_madt.h"

static uint32_t read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void clear_cpu_state(AcpiState* state) {
    state->cpu_parse_valid = 0;
    state->cpu_count = 0;
    state->cpu_total_count = 0;
    state->cpu_disabled_count = 0;
    state->cpu_overflow_count = 0;
    state->cpu_unsupported_count = 0;
    state->cpu_malformed_count = 0;
    for (uint32_t i = 0; i < ACPI_MAX_CPUS; i++) {
        state->cpus[i] = {};
    }
}

static void retain_cpu(AcpiState* state,
                       uint8_t entry_type,
                       uint32_t acpi_id,
                       uint32_t apic_id,
                       uint32_t flags) {
    state->cpu_total_count++;
    const uint8_t enabled = (flags & 1u) != 0;
    const uint8_t online_capable = (flags & 2u) != 0;
    if (!enabled && !online_capable) {
        state->cpu_disabled_count++;
        return;
    }
    if (entry_type == 9) {
        state->cpu_unsupported_count++;
    }
    if (state->cpu_count >= ACPI_MAX_CPUS) {
        state->cpu_overflow_count++;
        return;
    }
    AcpiCpuInfo* cpu = &state->cpus[state->cpu_count++];
    cpu->acpi_id = acpi_id;
    cpu->apic_id = apic_id;
    cpu->flags = flags;
    cpu->entry_type = entry_type;
    cpu->enabled = enabled;
    cpu->online_capable = online_capable;
    cpu->xapic_supported = entry_type == 0 ? 1u : 0u;
}

int acpi_madt_parse_cpu_entries(const uint8_t* entries,
                                uint32_t length,
                                AcpiState* state) {
    if (state == 0 || (entries == 0 && length != 0)) {
        return 0;
    }
    clear_cpu_state(state);
    uint32_t offset = 0;
    while (offset < length) {
        if (length - offset < 2u) {
            state->cpu_malformed_count++;
            state->cpu_count = 0;
            return 0;
        }
        const uint8_t type = entries[offset];
        const uint8_t entry_length = entries[offset + 1];
        if (entry_length < 2u || entry_length > length - offset) {
            state->cpu_malformed_count++;
            state->cpu_count = 0;
            return 0;
        }
        const uint8_t* data = entries + offset;
        if (type == 0) {
            if (entry_length < 8u) {
                state->cpu_malformed_count++;
                state->cpu_count = 0;
                return 0;
            }
            retain_cpu(state, type, data[2], data[3], read_u32(data + 4));
        } else if (type == 9) {
            if (entry_length < 16u) {
                state->cpu_malformed_count++;
                state->cpu_count = 0;
                return 0;
            }
            retain_cpu(state,
                       type,
                       read_u32(data + 12),
                       read_u32(data + 4),
                       read_u32(data + 8));
        }
        offset += entry_length;
    }
    state->cpu_parse_valid = 1;
    return 1;
}
