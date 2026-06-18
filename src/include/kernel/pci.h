#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 512
#define PCI_MAX_BARS 6

#define PCI_BAR_TYPE_NONE 0
#define PCI_BAR_TYPE_IO 1
#define PCI_BAR_TYPE_MMIO32 2
#define PCI_BAR_TYPE_MMIO64 3

struct PCIBarInfo {
    uint64_t base;
    uint64_t size;
    uint32_t type;
    uint32_t flags;
};

struct PCIDeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t header_type;
    uint8_t multifunction;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t bar_count;
    uint8_t reserved[3];
    uint32_t raw_bars[PCI_MAX_BARS];
};

#ifdef __cplusplus
extern "C" {
#endif

void pci_discover();
uint32_t pci_get_device_count();
const PCIDeviceInfo* pci_get_device(uint32_t index);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, PCIDeviceInfo* out);
int pci_get_bar(const PCIDeviceInfo* device, uint32_t bar_index, PCIBarInfo* out);
void* pci_map_bar(const PCIDeviceInfo* device, uint32_t bar_index, PCIBarInfo* out);
int pci_enable_memory_space(const PCIDeviceInfo* device);
int pci_enable_bus_mastering(const PCIDeviceInfo* device);

uint32_t pci_read_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset);
void pci_write_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset, uint32_t value);

#ifdef __cplusplus
}
#endif

void command_pci();

#endif
