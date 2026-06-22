#include "kernel/acpi.h"

extern "C" {
    #include "arch/x86_64/io.h"
}

#include "arch/x86_64/paging64.h"
#include "kernel/klog.h"
#include "kernel/kutil64.h"

#define ACPI_MAX_TABLE_LENGTH (1024u * 1024u)
#define ACPI_ADDRESS_SYSTEM_MEMORY 0u
#define ACPI_ADDRESS_SYSTEM_IO 1u
#define ACPI_PM1_SCI_ENABLE 0x0001u
#define ACPI_PM1_SLEEP_TYPE_MASK (7u << 10)
#define ACPI_PM1_SLEEP_ENABLE (1u << 13)

struct __attribute__((packed)) AcpiSdtHeader {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
};

struct __attribute__((packed)) AcpiGenericAddress {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
};

struct AcpiPowerState {
    uint8_t ready;
    uint8_t sleep_type_a;
    uint8_t sleep_type_b;
    uint8_t pm1_control_length;
    uint32_t smi_command_port;
    uint8_t acpi_enable_value;
    uint64_t fadt_address;
    uint64_t dsdt_address;
    AcpiGenericAddress pm1a_control;
    AcpiGenericAddress pm1b_control;
};

static AcpiPowerState power_state;

static void clear_bytes(void* pointer, uint32_t size) {
    uint8_t* bytes = (uint8_t*)pointer;
    for (uint32_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static int signature_equal(const char* value, const char* expected) {
    for (uint32_t i = 0; i < 4; i++) {
        if (value[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

static int map_table(uint64_t address, uint32_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return 0;
    }
    return paging64_map_range_identity(address, size, PAGING64_FLAG_NX);
}

static int checksum_valid(const void* pointer, uint32_t size) {
    if (pointer == 0 || size < sizeof(AcpiSdtHeader) ||
        size > ACPI_MAX_TABLE_LENGTH) {
        return 0;
    }
    const uint8_t* bytes = (const uint8_t*)pointer;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0;
}

static const AcpiSdtHeader* load_sdt(uint64_t address, const char* signature) {
    if (!map_table(address, sizeof(AcpiSdtHeader))) {
        return 0;
    }
    const AcpiSdtHeader* header = (const AcpiSdtHeader*)(uintptr_t)address;
    if (!signature_equal(header->signature, signature) ||
        header->length < sizeof(AcpiSdtHeader) ||
        header->length > ACPI_MAX_TABLE_LENGTH ||
        !map_table(address, header->length) ||
        !checksum_valid(header, header->length)) {
        return 0;
    }
    return header;
}

static uint32_t read_u32(const uint8_t* bytes, uint32_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1] << 8) |
           ((uint32_t)bytes[offset + 2] << 16) |
           ((uint32_t)bytes[offset + 3] << 24);
}

static uint64_t read_u64(const uint8_t* bytes, uint32_t offset) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; i++) {
        value |= (uint64_t)bytes[offset + i] << (i * 8u);
    }
    return value;
}

static AcpiGenericAddress read_gas(const uint8_t* bytes, uint32_t offset) {
    AcpiGenericAddress gas = {};
    gas.address_space = bytes[offset];
    gas.bit_width = bytes[offset + 1];
    gas.bit_offset = bytes[offset + 2];
    gas.access_size = bytes[offset + 3];
    gas.address = read_u64(bytes, offset + 4);
    return gas;
}

static int valid_control_register(const AcpiGenericAddress& gas) {
    if (gas.address == 0 || gas.bit_offset != 0 || gas.bit_width < 16) {
        return 0;
    }
    if (gas.address_space == ACPI_ADDRESS_SYSTEM_IO) {
        return gas.address <= 0xFFFFu;
    }
    return gas.address_space == ACPI_ADDRESS_SYSTEM_MEMORY;
}

static int parse_package_length(const uint8_t* aml,
                                uint32_t remaining,
                                uint32_t* length_out,
                                uint32_t* bytes_out) {
    if (remaining == 0 || length_out == 0 || bytes_out == 0) {
        return 0;
    }
    uint8_t lead = aml[0];
    uint32_t following = lead >> 6;
    if (following + 1u > remaining) {
        return 0;
    }
    uint32_t length = following == 0 ? (lead & 0x3Fu) : (lead & 0x0Fu);
    for (uint32_t i = 0; i < following; i++) {
        length |= (uint32_t)aml[i + 1] << (4u + i * 8u);
    }
    *length_out = length;
    *bytes_out = following + 1u;
    return 1;
}

static int parse_aml_integer(const uint8_t* aml,
                             uint32_t remaining,
                             uint64_t* value_out,
                             uint32_t* bytes_out) {
    if (remaining == 0 || value_out == 0 || bytes_out == 0) {
        return 0;
    }
    if (aml[0] == 0x00 || aml[0] == 0x01) {
        *value_out = aml[0];
        *bytes_out = 1;
        return 1;
    }

    uint32_t size = 0;
    if (aml[0] == 0x0A) {
        size = 1;
    } else if (aml[0] == 0x0B) {
        size = 2;
    } else if (aml[0] == 0x0C) {
        size = 4;
    } else if (aml[0] == 0x0E) {
        size = 8;
    } else {
        return 0;
    }
    if (remaining < size + 1u) {
        return 0;
    }
    uint64_t value = 0;
    for (uint32_t i = 0; i < size; i++) {
        value |= (uint64_t)aml[i + 1] << (i * 8u);
    }
    *value_out = value;
    *bytes_out = size + 1u;
    return 1;
}

static int parse_s5_package(const AcpiSdtHeader* dsdt,
                            uint8_t* type_a,
                            uint8_t* type_b) {
    const uint8_t* aml = (const uint8_t*)dsdt;
    for (uint32_t offset = sizeof(AcpiSdtHeader);
         offset + 7u < dsdt->length;
         offset++) {
        if (aml[offset] != 0x08) {
            continue;
        }
        uint32_t name = offset + 1u;
        if (aml[name] == 0x5C) {
            name++;
        }
        while (name < dsdt->length && aml[name] == 0x5E) {
            name++;
        }
        if (name + 5u >= dsdt->length ||
            aml[name] != '_' || aml[name + 1] != 'S' ||
            aml[name + 2] != '5' || aml[name + 3] != '_' ||
            aml[name + 4] != 0x12) {
            continue;
        }

        uint32_t package = name + 5u;
        uint32_t package_length = 0;
        uint32_t length_bytes = 0;
        if (!parse_package_length(aml + package,
                                  dsdt->length - package,
                                  &package_length,
                                  &length_bytes)) {
            continue;
        }
        uint32_t body = package + length_bytes;
        uint64_t package_end64 = (uint64_t)package + package_length;
        uint32_t package_end = package_end64 > dsdt->length
            ? dsdt->length
            : (uint32_t)package_end64;
        if (body >= package_end || aml[body] < 2u) {
            continue;
        }
        body++;
        uint64_t first = 0;
        uint64_t second = 0;
        uint32_t consumed = 0;
        if (!parse_aml_integer(aml + body, package_end - body,
                               &first, &consumed)) {
            continue;
        }
        body += consumed;
        if (!parse_aml_integer(aml + body, package_end - body,
                               &second, &consumed) ||
            first > 7u || second > 7u) {
            continue;
        }
        *type_a = (uint8_t)first;
        *type_b = (uint8_t)second;
        return 1;
    }
    return 0;
}

static int read_control(const AcpiGenericAddress& gas, uint16_t* value_out) {
    if (!valid_control_register(gas) || value_out == 0) {
        return 0;
    }
    if (gas.address_space == ACPI_ADDRESS_SYSTEM_IO) {
        *value_out = inw((uint16_t)gas.address);
        return 1;
    }
    uint64_t flags = PAGING64_FLAG_WRITABLE |
                     PAGING64_FLAG_CACHE_DISABLE |
                     PAGING64_FLAG_NX;
    if (!paging64_map_range_identity(gas.address, sizeof(uint16_t), flags)) {
        return 0;
    }
    *value_out = *(volatile uint16_t*)(uintptr_t)gas.address;
    return 1;
}

static int write_control(const AcpiGenericAddress& gas, uint16_t value) {
    if (!valid_control_register(gas)) {
        return 0;
    }
    if (gas.address_space == ACPI_ADDRESS_SYSTEM_IO) {
        outw((uint16_t)gas.address, value);
        return 1;
    }
    uint64_t flags = PAGING64_FLAG_WRITABLE |
                     PAGING64_FLAG_CACHE_DISABLE |
                     PAGING64_FLAG_NX;
    if (!paging64_map_range_identity(gas.address, sizeof(uint16_t), flags)) {
        return 0;
    }
    *(volatile uint16_t*)(uintptr_t)gas.address = value;
    return 1;
}

void acpi_power_reset() {
    clear_bytes(&power_state, sizeof(power_state));
}

int acpi_power_init(uint64_t fadt_address) {
    acpi_power_reset();
    const AcpiSdtHeader* header = load_sdt(fadt_address, "FACP");
    if (header == 0 || header->length < 90u) {
        return 0;
    }

    const uint8_t* fadt = (const uint8_t*)header;
    power_state.fadt_address = fadt_address;
    power_state.smi_command_port = read_u32(fadt, 48);
    power_state.acpi_enable_value = fadt[52];
    power_state.pm1_control_length = fadt[89];

    uint64_t dsdt_address = read_u32(fadt, 40);
    if (header->length >= 148u) {
        uint64_t x_dsdt = read_u64(fadt, 140);
        if (x_dsdt != 0) {
            dsdt_address = x_dsdt;
        }
    }

    if (header->length >= 196u) {
        power_state.pm1a_control = read_gas(fadt, 172);
        power_state.pm1b_control = read_gas(fadt, 184);
    }
    if (!valid_control_register(power_state.pm1a_control)) {
        power_state.pm1a_control.address_space = ACPI_ADDRESS_SYSTEM_IO;
        power_state.pm1a_control.bit_width =
            power_state.pm1_control_length != 0
                ? (uint8_t)(power_state.pm1_control_length * 8u)
                : 16u;
        power_state.pm1a_control.address = read_u32(fadt, 64);
    }
    if (!valid_control_register(power_state.pm1b_control)) {
        power_state.pm1b_control.address_space = ACPI_ADDRESS_SYSTEM_IO;
        power_state.pm1b_control.bit_width =
            power_state.pm1_control_length != 0
                ? (uint8_t)(power_state.pm1_control_length * 8u)
                : 16u;
        power_state.pm1b_control.address = read_u32(fadt, 68);
    }
    if (!valid_control_register(power_state.pm1a_control)) {
        return 0;
    }

    const AcpiSdtHeader* dsdt = load_sdt(dsdt_address, "DSDT");
    if (dsdt == 0 || !parse_s5_package(dsdt,
                                       &power_state.sleep_type_a,
                                       &power_state.sleep_type_b)) {
        return 0;
    }
    power_state.dsdt_address = dsdt_address;
    power_state.ready = 1;
    klog_write(KLOG_INFO, "acpi", "ACPI S5 power-off ready");
    return 1;
}

int acpi_power_available() {
    return power_state.ready != 0;
}

static int enable_acpi_mode() {
    uint16_t control = 0;
    if (!read_control(power_state.pm1a_control, &control)) {
        return 0;
    }
    if (control & ACPI_PM1_SCI_ENABLE) {
        return 1;
    }
    if (power_state.smi_command_port == 0 ||
        power_state.smi_command_port > 0xFFFFu ||
        power_state.acpi_enable_value == 0) {
        return 0;
    }

    outb((uint16_t)power_state.smi_command_port,
         power_state.acpi_enable_value);
    for (uint32_t attempt = 0; attempt < 1000000u; attempt++) {
        if (read_control(power_state.pm1a_control, &control) &&
            (control & ACPI_PM1_SCI_ENABLE)) {
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int write_sleep_control(const AcpiGenericAddress& gas, uint8_t type) {
    uint16_t control = 0;
    if (!valid_control_register(gas)) {
        return 1;
    }
    if (!read_control(gas, &control)) {
        return 0;
    }
    control &= (uint16_t)~ACPI_PM1_SLEEP_TYPE_MASK;
    control |= (uint16_t)((uint16_t)type << 10);
    control |= ACPI_PM1_SLEEP_ENABLE;
    return write_control(gas, control);
}

int acpi_poweroff() {
    if (!power_state.ready || !enable_acpi_mode()) {
        return 0;
    }
    klog_write(KLOG_INFO, "power", "entering ACPI S5");
    if (!write_sleep_control(power_state.pm1b_control,
                             power_state.sleep_type_b) ||
        !write_sleep_control(power_state.pm1a_control,
                             power_state.sleep_type_a)) {
        return 0;
    }
    return 1;
}

void acpi_power_print() {
    print("\npower_s5=");
    print_hex32(power_state.ready);
    print(" dsdt=");
    print_hex64(power_state.dsdt_address);
    print(" pm1a=");
    print_hex64(power_state.pm1a_control.address);
    print(" pm1b=");
    print_hex64(power_state.pm1b_control.address);
    print(" type_a=");
    print_hex32(power_state.sleep_type_a);
    print(" type_b=");
    print_hex32(power_state.sleep_type_b);
}
