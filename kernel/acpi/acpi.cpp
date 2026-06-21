#include "kernel/acpi.h"
#include "arch/x86_64/paging64.h"
#include "kernel/klog.h"
#include "kernel/kutil64.h"

#define ACPI_MAX_TABLE_LENGTH (1024u * 1024u)

struct __attribute__((packed)) AcpiRsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
};

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

struct __attribute__((packed)) AcpiMadt {
    AcpiSdtHeader header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
};

struct __attribute__((packed)) MadtEntryHeader {
    uint8_t type;
    uint8_t length;
};

static AcpiState state;
static uint64_t known_rsdp_address = 0;
static uint64_t known_madt_address = 0;

static int map_physical(uint64_t address, uint64_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return 0;
    }
    return paging64_map_range_identity(address, size, PAGING64_FLAG_NX);
}

static int bytes_equal(const char* left, const char* right, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (left[i] != right[i]) {
            return 0;
        }
    }
    return 1;
}

static int checksum_valid(const void* data, uint32_t length) {
    if (data == 0 || length == 0 || length > ACPI_MAX_TABLE_LENGTH) {
        return 0;
    }
    const uint8_t* bytes = (const uint8_t*)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0;
}

static void update_sdt_checksum(AcpiSdtHeader* header) {
    header->checksum = 0;
    uint8_t sum = 0;
    const uint8_t* bytes = (const uint8_t*)header;
    for (uint32_t i = 0; i < header->length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    header->checksum = (uint8_t)(0u - sum);
}

static int remap_table(uint64_t address, uint32_t length, int writable) {
    uint64_t flags = PAGING64_FLAG_NX;
    if (writable) {
        flags |= PAGING64_FLAG_WRITABLE;
    }
    return paging64_remap_range(address, length, flags);
}

static int valid_sdt(const AcpiSdtHeader* header) {
    return header != 0 &&
           header->length >= sizeof(AcpiSdtHeader) &&
           header->length <= ACPI_MAX_TABLE_LENGTH &&
           checksum_valid(header, header->length);
}

static const AcpiSdtHeader* map_sdt(uint64_t address) {
    if (!map_physical(address, sizeof(AcpiSdtHeader))) {
        return 0;
    }
    const AcpiSdtHeader* header = (const AcpiSdtHeader*)(uintptr_t)address;
    if (header->length < sizeof(AcpiSdtHeader) ||
        header->length > ACPI_MAX_TABLE_LENGTH ||
        !map_physical(address, header->length)) {
        return 0;
    }
    return valid_sdt(header) ? header : 0;
}

static void clear_state() {
    uint8_t* bytes = (uint8_t*)&state;
    for (uint32_t i = 0; i < sizeof(state); i++) {
        bytes[i] = 0;
    }
}

static void parse_madt(const AcpiMadt* madt) {
    state.madt_address = (uint64_t)(uintptr_t)madt;
    state.local_apic_address = madt->local_apic_address;

    uint32_t offset = sizeof(AcpiMadt);
    while (offset + sizeof(MadtEntryHeader) <= madt->header.length) {
        const MadtEntryHeader* entry =
            (const MadtEntryHeader*)((const uint8_t*)madt + offset);
        if (entry->length < sizeof(MadtEntryHeader) ||
            offset + entry->length > madt->header.length) {
            break;
        }

        const uint8_t* data = (const uint8_t*)entry;
        if (entry->type == 0 && entry->length >= 8) {
            uint32_t flags = *(const uint32_t*)(const void*)(data + 4);
            if (flags & 0x3u) {
                state.cpu_count++;
            }
        } else if (entry->type == 1 && entry->length >= 12 &&
                   state.ioapic_count < ACPI_MAX_IOAPICS) {
            AcpiIoApicInfo* ioapic = &state.ioapics[state.ioapic_count++];
            ioapic->id = data[2];
            ioapic->address = *(const uint32_t*)(const void*)(data + 4);
            ioapic->gsi_base = *(const uint32_t*)(const void*)(data + 8);
        } else if (entry->type == 2 && entry->length >= 10 &&
                   state.override_count < ACPI_MAX_ISO) {
            AcpiInterruptOverride* iso = &state.overrides[state.override_count++];
            iso->bus = data[2];
            iso->source_irq = data[3];
            iso->gsi = *(const uint32_t*)(const void*)(data + 4);
            iso->flags = *(const uint16_t*)(const void*)(data + 8);
        } else if (entry->type == 5 && entry->length >= 12) {
            state.local_apic_address = *(const uint64_t*)(const void*)(data + 4);
        }
        offset += entry->length;
    }
}

int acpi_init(uint64_t rsdp_address) {
    clear_state();
    state.rsdp_address = rsdp_address;
    if (rsdp_address == 0) {
        klog_write(KLOG_WARN, "acpi", "RSDP was not supplied by UEFI");
        return 0;
    }

    if (!map_physical(rsdp_address, sizeof(AcpiRsdp))) {
        klog_write(KLOG_ERROR, "acpi", "failed to map RSDP");
        return 0;
    }

    known_rsdp_address = rsdp_address;

    const AcpiRsdp* rsdp = (const AcpiRsdp*)(uintptr_t)rsdp_address;
    if (!bytes_equal(rsdp->signature, "RSD PTR ", 8) ||
        !checksum_valid(rsdp, 20)) {
        klog_write(KLOG_ERROR, "acpi", "invalid RSDP signature or checksum");
        return 0;
    }
    if (rsdp->revision >= 2 &&
        (rsdp->length < sizeof(AcpiRsdp) || !checksum_valid(rsdp, rsdp->length))) {
        klog_write(KLOG_ERROR, "acpi", "invalid extended RSDP checksum");
        return 0;
    }

    state.revision = rsdp->revision;
    int xsdt = rsdp->revision >= 2 && rsdp->xsdt_address != 0;
    state.root_table_address = xsdt ? rsdp->xsdt_address : rsdp->rsdt_address;
    const AcpiSdtHeader* root = map_sdt(state.root_table_address);
    if (root == 0 ||
        !bytes_equal(root->signature, xsdt ? "XSDT" : "RSDT", 4)) {
        klog_write(KLOG_ERROR, "acpi", "invalid root system description table");
        return 0;
    }

    uint32_t entry_size = xsdt ? 8u : 4u;
    uint32_t count = (root->length - sizeof(AcpiSdtHeader)) / entry_size;
    const uint8_t* entries = (const uint8_t*)root + sizeof(AcpiSdtHeader);
    for (uint32_t i = 0; i < count; i++) {
        uint64_t address = xsdt
            ? *(const uint64_t*)(const void*)(entries + i * entry_size)
            : *(const uint32_t*)(const void*)(entries + i * entry_size);
        const AcpiSdtHeader* table = map_sdt(address);
        if (table != 0 && bytes_equal(table->signature, "APIC", 4)) {
            known_madt_address = address;
            parse_madt((const AcpiMadt*)table);
            break;
        }
    }

    state.ready = state.madt_address != 0 &&
                  state.local_apic_address != 0 &&
                  state.ioapic_count != 0;
    klog_write(state.ready ? KLOG_INFO : KLOG_WARN,
               "acpi",
               state.ready ? "MADT parsed" : "MADT or IOAPIC unavailable");
    return state.ready;
}

static AcpiMadt* prepare_debug_madt() {
    if (known_rsdp_address == 0 || !acpi_init(known_rsdp_address) ||
        known_madt_address == 0) {
        return 0;
    }
    AcpiMadt* madt = (AcpiMadt*)(uintptr_t)known_madt_address;
    if (!remap_table(known_madt_address, madt->header.length, 1)) {
        return 0;
    }
    return madt;
}

int acpi_debug_corrupt_rsdp_checksum() {
    if (known_rsdp_address == 0 || !acpi_init(known_rsdp_address)) {
        return 0;
    }

    AcpiRsdp* rsdp = (AcpiRsdp*)(uintptr_t)known_rsdp_address;
    uint32_t length = rsdp->revision >= 2 ? rsdp->length : 20u;
    if (!remap_table(known_rsdp_address, length, 1)) {
        return 0;
    }

    uint8_t* checksum = rsdp->revision >= 2
        ? &rsdp->extended_checksum
        : &rsdp->checksum;
    uint8_t saved = *checksum;
    *checksum ^= 0x01u;
    int rejected = !acpi_init(known_rsdp_address);
    if (!remap_table(known_rsdp_address, length, 1)) {
        return 0;
    }
    *checksum = saved;
    remap_table(known_rsdp_address, length, 0);
    return rejected;
}

int acpi_debug_corrupt_madt_entry_length() {
    AcpiMadt* madt = prepare_debug_madt();
    if (madt == 0) {
        return 0;
    }

    uint32_t offset = sizeof(AcpiMadt);
    while (offset + sizeof(MadtEntryHeader) <= madt->header.length) {
        MadtEntryHeader* entry =
            (MadtEntryHeader*)((uint8_t*)madt + offset);
        if (entry->length < sizeof(MadtEntryHeader) ||
            offset + entry->length > madt->header.length) {
            break;
        }
        if (entry->type == 1) {
            uint8_t saved_length = entry->length;
            uint8_t saved_checksum = madt->header.checksum;
            entry->length = 0;
            update_sdt_checksum(&madt->header);
            int rejected = !acpi_init(known_rsdp_address);
            if (!remap_table(known_madt_address, madt->header.length, 1)) {
                return 0;
            }
            entry->length = saved_length;
            madt->header.checksum = saved_checksum;
            remap_table(known_madt_address, madt->header.length, 0);
            return rejected;
        }
        offset += entry->length;
    }

    remap_table(known_madt_address, madt->header.length, 0);
    return 0;
}

int acpi_debug_remove_ioapics() {
    AcpiMadt* madt = prepare_debug_madt();
    if (madt == 0) {
        return 0;
    }

    uint32_t offsets[32];
    uint32_t count = 0;
    uint32_t offset = sizeof(AcpiMadt);
    while (offset + sizeof(MadtEntryHeader) <= madt->header.length) {
        MadtEntryHeader* entry =
            (MadtEntryHeader*)((uint8_t*)madt + offset);
        if (entry->length < sizeof(MadtEntryHeader) ||
            offset + entry->length > madt->header.length) {
            break;
        }
        if (entry->type == 1) {
            if (count >= sizeof(offsets) / sizeof(offsets[0])) {
                remap_table(known_madt_address, madt->header.length, 0);
                return 0;
            }
            offsets[count++] = offset;
            entry->type = 0xFFu;
        }
        offset += entry->length;
    }
    if (count == 0) {
        remap_table(known_madt_address, madt->header.length, 0);
        return 0;
    }

    uint8_t saved_checksum = madt->header.checksum;
    update_sdt_checksum(&madt->header);
    int rejected = !acpi_init(known_rsdp_address);
    if (!remap_table(known_madt_address, madt->header.length, 1)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        MadtEntryHeader* entry =
            (MadtEntryHeader*)((uint8_t*)madt + offsets[i]);
        entry->type = 1;
    }
    madt->header.checksum = saved_checksum;
    remap_table(known_madt_address, madt->header.length, 0);
    return rejected;
}

const AcpiState* acpi_state() {
    return &state;
}

const AcpiInterruptOverride* acpi_find_override(uint8_t source_irq) {
    for (uint32_t i = 0; i < state.override_count; i++) {
        if (state.overrides[i].source_irq == source_irq) {
            return &state.overrides[i];
        }
    }
    return 0;
}

void acpi_print_summary() {
    print("\n=== ACPI ===");
    print("\nready=");
    print_hex32(state.ready);
    print(" revision=");
    print_hex32(state.revision);
    print(" cpus=");
    print_hex32(state.cpu_count);
    print("\nrsdp=");
    print_hex64(state.rsdp_address);
    print(" root=");
    print_hex64(state.root_table_address);
    print("\nmadt=");
    print_hex64(state.madt_address);
    print(" lapic=");
    print_hex64(state.local_apic_address);
    for (uint32_t i = 0; i < state.ioapic_count; i++) {
        print("\nioapic[");
        print_hex32(i);
        print("] id=");
        print_hex32(state.ioapics[i].id);
        print(" addr=");
        print_hex32(state.ioapics[i].address);
        print(" gsi_base=");
        print_hex32(state.ioapics[i].gsi_base);
    }
    for (uint32_t i = 0; i < state.override_count; i++) {
        print("\niso[");
        print_hex32(i);
        print("] irq=");
        print_hex32(state.overrides[i].source_irq);
        print(" gsi=");
        print_hex32(state.overrides[i].gsi);
        print(" flags=");
        print_hex32(state.overrides[i].flags);
    }
    print("\n============\n");
}
