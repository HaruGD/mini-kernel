#include "arch/x86_64/io.h"
#include "arch/x86_64/paging64.h"
#include "kernel/kutil64.h"
#include "kernel/pci.h"

static PCIDeviceInfo g_pci_devices[PCI_MAX_DEVICES];
static uint32_t g_pci_device_count = 0;
static uint8_t g_pci_discovered = 0;

#define PCI_MMIO_MAP_BASE  0x0000000060000000ULL
#define PCI_MMIO_MAP_LIMIT 0x0000000070000000ULL

static uint64_t g_pci_mmio_next_virtual = PCI_MMIO_MAP_BASE;

typedef struct {
    uint16_t vendor_id;
    const char* name;
} PCIVendorName;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char* name;
} PCIDeviceName;

static const PCIVendorName g_pci_vendor_names[] = {
    {0x8086U, "Intel"},
    {0x1234U, "Bochs/QEMU"},
    {0x1AF4U, "Virtio"},
    {0x10ECU, "Realtek"},
    {0x80EEU, "VirtualBox"},
    {0x1022U, "AMD"},
    {0x10DEU, "NVIDIA"},
};

static const PCIDeviceName g_pci_device_names[] = {
    {0x8086U, 0x1237U, "440FX - 82441FX PMC"},
    {0x8086U, 0x7000U, "82371SB PIIX3 ISA"},
    {0x8086U, 0x7010U, "82371SB PIIX3 IDE"},
    {0x8086U, 0x7113U, "82371AB/EB/MB PIIX4 ACPI"},
    {0x8086U, 0x100EU, "82540EM Gigabit Ethernet"},
    {0x1234U, 0x1111U, "Bochs VGA"},
    {0x1AF4U, 0x1001U, "virtio network device"},
};

static uint32_t pci_make_address(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset) {
    return 0x80000000U |
           (((uint32_t)bus & 0xFFU) << 16) |
           (((uint32_t)device & 0x1FU) << 11) |
           (((uint32_t)function & 0x07U) << 8) |
           ((uint32_t)offset & 0xFCU);
}

static int pci_present(uint32_t vendor_device) {
    return vendor_device != 0xFFFFFFFFU && (vendor_device & 0xFFFFU) != 0xFFFFU;
}

static uint64_t pci_align_up(uint64_t value, uint64_t align) {
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static void pci_clear_device(PCIDeviceInfo* device) {
    if (device == 0) {
        return;
    }
    device->vendor_id = 0xFFFFU;
    device->device_id = 0xFFFFU;
    device->command = 0;
    device->status = 0;
    device->bus = 0;
    device->device = 0;
    device->function = 0;
    device->revision_id = 0;
    device->prog_if = 0;
    device->subclass = 0;
    device->class_code = 0;
    device->header_type = 0;
    device->multifunction = 0;
    device->irq_line = 0;
    device->irq_pin = 0;
    device->bar_count = 0;
    for (uint32_t i = 0; i < PCI_MAX_BARS; i++) {
        device->raw_bars[i] = 0;
    }
}

uint32_t pci_read_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset) {
    outl(0xCF8, pci_make_address(bus, device, function, offset));
    return inl(0xCFC);
}

void pci_write_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset, uint32_t value) {
    outl(0xCF8, pci_make_address(bus, device, function, offset));
    outl(0xCFC, value);
}

static const char* pci_vendor_name(uint16_t vendor_id) {
    for (uint32_t i = 0; i < sizeof(g_pci_vendor_names) / sizeof(g_pci_vendor_names[0]); i++) {
        if (g_pci_vendor_names[i].vendor_id == vendor_id) {
            return g_pci_vendor_names[i].name;
        }
    }
    return 0;
}

static const char* pci_device_name(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < sizeof(g_pci_device_names) / sizeof(g_pci_device_names[0]); i++) {
        if (g_pci_device_names[i].vendor_id == vendor_id && g_pci_device_names[i].device_id == device_id) {
            return g_pci_device_names[i].name;
        }
    }
    return 0;
}

static uint8_t pci_header_type(uint64_t bus, uint64_t device, uint64_t function) {
    uint32_t value = pci_read_config32(bus, device, function, 0x0C);
    return (uint8_t)((value >> 16) & 0xFFU);
}

static void pci_read_common_fields(PCIDeviceInfo* out, uint64_t bus, uint64_t device, uint64_t function) {
    uint32_t id = pci_read_config32(bus, device, function, 0x00);
    uint32_t command = pci_read_config32(bus, device, function, 0x04);
    uint32_t class_reg = pci_read_config32(bus, device, function, 0x08);
    uint32_t header_reg = pci_read_config32(bus, device, function, 0x0C);
    uint32_t irq_reg = pci_read_config32(bus, device, function, 0x3C);

    out->vendor_id = (uint16_t)(id & 0xFFFFU);
    out->device_id = (uint16_t)((id >> 16) & 0xFFFFU);
    out->command = (uint16_t)(command & 0xFFFFU);
    out->status = (uint16_t)((command >> 16) & 0xFFFFU);
    out->revision_id = (uint8_t)(class_reg & 0xFFU);
    out->prog_if = (uint8_t)((class_reg >> 8) & 0xFFU);
    out->subclass = (uint8_t)((class_reg >> 16) & 0xFFU);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFFU);
    out->header_type = (uint8_t)((header_reg >> 16) & 0x7FU);
    out->multifunction = (uint8_t)(((header_reg >> 23) & 0x01U) != 0);
    out->irq_line = (uint8_t)(irq_reg & 0xFFU);
    out->irq_pin = (uint8_t)((irq_reg >> 8) & 0xFFU);
    out->bus = (uint8_t)bus;
    out->device = (uint8_t)device;
    out->function = (uint8_t)function;
    out->bar_count = (out->header_type == 0x00U) ? 6U : ((out->header_type == 0x01U) ? 2U : 0U);

    for (uint32_t i = 0; i < PCI_MAX_BARS; i++) {
        out->raw_bars[i] = pci_read_config32(bus, device, function, 0x10 + (uint64_t)i * 4ULL);
    }
}

void pci_discover() {
    g_pci_device_count = 0;
    g_pci_discovered = 1;
    for (uint32_t i = 0; i < PCI_MAX_DEVICES; i++) {
        pci_clear_device(&g_pci_devices[i]);
    }

    for (uint64_t bus = 0; bus < 256; bus++) {
        for (uint64_t device = 0; device < 32; device++) {
            uint32_t vendor_device = pci_read_config32(bus, device, 0, 0x00);
            if (!pci_present(vendor_device)) {
                continue;
            }

            uint8_t header = pci_header_type(bus, device, 0);
            uint32_t function_count = (header & 0x80U) ? 8U : 1U;
            for (uint64_t function = 0; function < function_count; function++) {
                vendor_device = pci_read_config32(bus, device, function, 0x00);
                if (!pci_present(vendor_device)) {
                    continue;
                }
                if (g_pci_device_count >= PCI_MAX_DEVICES) {
                    return;
                }

                PCIDeviceInfo* out = &g_pci_devices[g_pci_device_count++];
                pci_clear_device(out);
                pci_read_common_fields(out, bus, device, function);
            }
        }
    }
}

uint32_t pci_get_device_count() {
    if (!g_pci_discovered) {
        pci_discover();
    }
    return g_pci_device_count;
}

const PCIDeviceInfo* pci_get_device(uint32_t index) {
    if (!g_pci_discovered) {
        pci_discover();
    }
    if (index >= g_pci_device_count) {
        return 0;
    }
    return &g_pci_devices[index];
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, PCIDeviceInfo* out) {
    if (out == 0) {
        return 0;
    }
    if (!g_pci_discovered) {
        pci_discover();
    }
    for (uint32_t i = 0; i < g_pci_device_count; i++) {
        const PCIDeviceInfo* device = &g_pci_devices[i];
        if (device->vendor_id == vendor_id && device->device_id == device_id) {
            *out = *device;
            return 1;
        }
    }
    return 0;
}

static uint64_t pci_bar_offset(uint32_t bar_index) {
    return 0x10ULL + (uint64_t)bar_index * 4ULL;
}

static uint64_t pci_restore_bar64(uint64_t bus, uint64_t device, uint64_t function, uint32_t bar_index, uint32_t raw) {
    pci_write_config32(bus, device, function, pci_bar_offset(bar_index), raw);
    return 0;
}

static int pci_decode_io_bar(uint64_t bus,
                             uint64_t device,
                             uint64_t function,
                             uint32_t bar_index,
                             uint32_t raw,
                             PCIBarInfo* out) {
    uint32_t original = raw;
    pci_write_config32(bus, device, function, pci_bar_offset(bar_index), 0xFFFFFFFFU);
    uint32_t mask = pci_read_config32(bus, device, function, pci_bar_offset(bar_index));
    pci_restore_bar64(bus, device, function, bar_index, original);

    if (mask == 0 || mask == 0xFFFFFFFFU) {
        out->base = (uint64_t)(raw & ~0x3U);
        out->size = 0;
    } else {
        out->base = (uint64_t)(raw & ~0x3U);
        out->size = (uint64_t)((~(mask & ~0x3U)) + 1U) & ~0x3ULL;
    }
    out->type = PCI_BAR_TYPE_IO;
    out->flags = raw & 0x3U;
    return 1;
}

static int pci_decode_mmio32_bar(uint64_t bus,
                                 uint64_t device,
                                 uint64_t function,
                                 uint32_t bar_index,
                                 uint32_t raw,
                                 PCIBarInfo* out) {
    uint32_t original = raw;
    pci_write_config32(bus, device, function, pci_bar_offset(bar_index), 0xFFFFFFFFU);
    uint32_t mask = pci_read_config32(bus, device, function, pci_bar_offset(bar_index));
    pci_restore_bar64(bus, device, function, bar_index, original);

    out->base = (uint64_t)(raw & ~0x0FU);
    if (mask == 0 || mask == 0xFFFFFFFFU) {
        out->size = 0;
    } else {
        out->size = (uint64_t)((~(mask & ~0x0FU)) + 1U) & ~0xFULL;
    }
    out->type = PCI_BAR_TYPE_MMIO32;
    out->flags = raw & 0x0FU;
    return 1;
}

static int pci_decode_mmio64_bar(uint64_t bus,
                                 uint64_t device,
                                 uint64_t function,
                                 uint32_t bar_index,
                                 uint32_t raw_low,
                                 uint32_t raw_high,
                                 PCIBarInfo* out) {
    uint32_t original_low = raw_low;
    uint32_t original_high = raw_high;
    pci_write_config32(bus, device, function, pci_bar_offset(bar_index), 0xFFFFFFFFU);
    pci_write_config32(bus, device, function, pci_bar_offset(bar_index + 1), 0xFFFFFFFFU);
    uint32_t mask_low = pci_read_config32(bus, device, function, pci_bar_offset(bar_index));
    uint32_t mask_high = pci_read_config32(bus, device, function, pci_bar_offset(bar_index + 1));
    pci_restore_bar64(bus, device, function, bar_index, original_low);
    pci_restore_bar64(bus, device, function, bar_index + 1, original_high);

    uint64_t base = ((uint64_t)raw_high << 32) | (uint64_t)(raw_low & ~0x0FU);
    uint64_t mask = ((uint64_t)mask_high << 32) | (uint64_t)(mask_low & ~0x0FU);
    out->base = base;
    if (mask == 0 || mask == 0xFFFFFFFFFFFFFFFFULL) {
        out->size = 0;
    } else {
        out->size = (~mask) + 1ULL;
    }
    out->type = PCI_BAR_TYPE_MMIO64;
    out->flags = raw_low & 0x0FU;
    return 1;
}

static void* pci_map_physical_range(uint64_t phys, uint64_t size) {
    if (phys == 0) {
        return 0;
    }
    if (size == 0) {
        size = PAGING64_PAGE_SIZE;
    }

    uint64_t aligned_phys = phys & ~(PAGING64_PAGE_SIZE - 1ULL);
    uint64_t offset = phys - aligned_phys;
    uint64_t total_bytes = pci_align_up(offset + size, PAGING64_PAGE_SIZE);
    uint64_t virt_base = pci_align_up(g_pci_mmio_next_virtual, PAGING64_PAGE_SIZE);
    uint64_t virt_end = virt_base + total_bytes;
    if (virt_end > PCI_MMIO_MAP_LIMIT) {
        return 0;
    }

    if (!paging64_map_range(virt_base,
                            aligned_phys,
                            total_bytes,
                            PAGING64_FLAG_WRITABLE |
                            PAGING64_FLAG_WRITE_THROUGH |
                            PAGING64_FLAG_CACHE_DISABLE |
                            PAGING64_FLAG_NX)) {
        return 0;
    }

    g_pci_mmio_next_virtual = virt_end;
    return (void*)(uintptr_t)(virt_base + offset);
}

int pci_get_bar(const PCIDeviceInfo* device_info, uint32_t bar_index, PCIBarInfo* out) {
    if (device_info == 0 || out == 0) {
        return 0;
    }
    if (bar_index >= device_info->bar_count || bar_index >= PCI_MAX_BARS) {
        return 0;
    }

    uint64_t bus = device_info->bus;
    uint64_t device = device_info->device;
    uint64_t function = device_info->function;
    uint32_t raw = device_info->raw_bars[bar_index];

    if (raw == 0 || raw == 0xFFFFFFFFU) {
        return 0;
    }
    if (raw & 0x1U) {
        return pci_decode_io_bar(bus, device, function, bar_index, raw, out);
    }

    uint32_t mem_type = (raw >> 1) & 0x3U;
    if (mem_type == 0x2U && bar_index + 1 < device_info->bar_count) {
        return pci_decode_mmio64_bar(bus,
                                     device,
                                     function,
                                     bar_index,
                                     raw,
                                     device_info->raw_bars[bar_index + 1],
                                     out);
    }
    return pci_decode_mmio32_bar(bus, device, function, bar_index, raw, out);
}

void* pci_map_bar(const PCIDeviceInfo* device_info, uint32_t bar_index, PCIBarInfo* out) {
    PCIBarInfo local_bar;
    PCIBarInfo* bar = out != 0 ? out : &local_bar;
    if (!pci_get_bar(device_info, bar_index, bar)) {
        return 0;
    }
    if (bar->type == PCI_BAR_TYPE_IO || bar->base == 0) {
        return 0;
    }
    if (!pci_enable_memory_space(device_info)) {
        return 0;
    }
    return pci_map_physical_range(bar->base, bar->size);
}

static int pci_update_command_bits(const PCIDeviceInfo* device_info, uint16_t mask) {
    if (device_info == 0) {
        return 0;
    }

    uint64_t bus = device_info->bus;
    uint64_t device = device_info->device;
    uint64_t function = device_info->function;
    uint32_t value = pci_read_config32(bus, device, function, 0x04);
    uint16_t command = (uint16_t)(value & 0xFFFFU);
    command |= mask;
    value = (value & 0xFFFF0000U) | (uint32_t)command;
    pci_write_config32(bus, device, function, 0x04, value);
    return 1;
}

int pci_enable_memory_space(const PCIDeviceInfo* device_info) {
    return pci_update_command_bits(device_info, 0x0002U);
}

int pci_enable_bus_mastering(const PCIDeviceInfo* device_info) {
    return pci_update_command_bits(device_info, 0x0004U);
}

static const char* pci_class_name(uint8_t class_code, uint8_t subclass) {
    if (class_code == 0x01U && subclass == 0x06U) return "SATA";
    if (class_code == 0x01U && subclass == 0x01U) return "IDE";
    if (class_code == 0x02U) return "network";
    if (class_code == 0x03U) return "display";
    if (class_code == 0x04U) return "multimedia";
    if (class_code == 0x06U && subclass == 0x00U) return "host bridge";
    if (class_code == 0x06U && subclass == 0x01U) return "ISA bridge";
    if (class_code == 0x06U && subclass == 0x04U) return "PCI bridge";
    if (class_code == 0x06U && subclass == 0x80U) return "bridge";
    if (class_code == 0x0CU && subclass == 0x03U) return "USB";
    return "device";
}

static void pci_print_hex8(uint8_t value) {
    print_hex32((uint32_t)value);
}

static void print_pci_bar(const PCIBarInfo* bar, uint32_t index) {
    print("\n    bar");
    print_hex32(index);
    print(" type=");
    if (bar->type == PCI_BAR_TYPE_IO) {
        print("io");
    } else if (bar->type == PCI_BAR_TYPE_MMIO64) {
        print("mmio64");
    } else if (bar->type == PCI_BAR_TYPE_MMIO32) {
        print("mmio32");
    } else {
        print("none");
    }
    print(" base=");
    print_hex64(bar->base);
    print(" size=");
    print_hex64(bar->size);
    print(" flags=");
    print_hex32(bar->flags);
}

void command_pci() {
    uint32_t count = pci_get_device_count();
    print("\n=== PCI ===");
    print("\ncount=");
    print_hex32(count);
    for (uint32_t i = 0; i < count; i++) {
        const PCIDeviceInfo* device = pci_get_device(i);
        if (device == 0) {
            continue;
        }
        print("\n[");
        print_hex32(i);
        print("] bus=");
        pci_print_hex8(device->bus);
        print(" dev=");
        pci_print_hex8(device->device);
        print(" fn=");
        pci_print_hex8(device->function);
        print(" vendor=");
        const char* vendor_name = pci_vendor_name(device->vendor_id);
        if (vendor_name != 0) {
            print(vendor_name);
            print(" ");
        }
        print_hex32(device->vendor_id);
        print(" device=");
        const char* device_name = pci_device_name(device->vendor_id, device->device_id);
        if (device_name != 0) {
            print(device_name);
            print(" ");
        }
        print_hex32(device->device_id);
        print("\n    class=");
        print(pci_class_name(device->class_code, device->subclass));
        print(" code=");
        print_hex32(device->class_code);
        print(" subclass=");
        print_hex32(device->subclass);
        print(" prog_if=");
        print_hex32(device->prog_if);
        print(" hdr=");
        print_hex32(device->header_type);
        print(" mf=");
        print_hex32(device->multifunction);
        print(" irq=");
        print_hex32(device->irq_line);
        print(" pin=");
        print_hex32(device->irq_pin);

        for (uint32_t bar = 0; bar < device->bar_count && bar < PCI_MAX_BARS; bar++) {
            PCIBarInfo info;
            if (pci_get_bar(device, bar, &info)) {
                print_pci_bar(&info, bar);
            }
        }
    }
    print("\n==============");
}
