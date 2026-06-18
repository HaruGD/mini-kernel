#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/kernel_exports.h"
#include "kernel/kutil64.h"
#include "drivers/gop.h"
#include "kernel/pci.h"
#include "drivers/ata.h"
#include "fs/vfs.h"
#include <stddef.h>

extern "C" {
    #include "heap.h"
}

extern ATADriver ata;

extern "C" void driver_klog(const char* text) {
    print("\n[drv] ");
    print(text != 0 ? text : "(null)");
}

extern "C" void* driver_kmalloc(uint64_t size) {
    return kmalloc((size_t)size);
}

extern "C" void driver_kfree(void* ptr) {
    kfree(ptr);
}

extern "C" const GOPInfo* driver_gop_get_info() {
    return gop.info();
}

extern "C" void driver_gop_clear(uint32_t color) {
    gop.clear(color);
}

extern "C" void driver_gop_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    gop.putpixel(x, y, color);
}

extern "C" void driver_gop_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    gop.fill_rect(x, y, width, height, color);
}

extern "C" uint32_t driver_pci_read_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset) {
    return pci_read_config32(bus, device, function, offset);
}

extern "C" void driver_pci_write_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset, uint32_t value) {
    pci_write_config32(bus, device, function, offset, value);
}

extern "C" uint64_t driver_pci_device_count() {
    return pci_get_device_count();
}

extern "C" int64_t driver_pci_get_device(uint64_t index, PCIDeviceInfo* out) {
    const PCIDeviceInfo* device = pci_get_device((uint32_t)index);
    if (device == 0 || out == 0) {
        return -1;
    }
    *out = *device;
    return 0;
}

extern "C" int64_t driver_pci_find_device(uint64_t vendor_id, uint64_t device_id, PCIDeviceInfo* out) {
    return pci_find_device((uint16_t)vendor_id, (uint16_t)device_id, out) ? 0 : -1;
}

extern "C" int64_t driver_pci_get_bar(const PCIDeviceInfo* device, uint64_t bar_index, PCIBarInfo* out) {
    return pci_get_bar(device, (uint32_t)bar_index, out) ? 0 : -1;
}

extern "C" void* driver_pci_map_bar(const PCIDeviceInfo* device, uint64_t bar_index, PCIBarInfo* out) {
    return pci_map_bar(device, (uint32_t)bar_index, out);
}

extern "C" int64_t driver_pci_enable_memory_space(const PCIDeviceInfo* device) {
    return pci_enable_memory_space(device) ? 0 : -1;
}

extern "C" int64_t driver_pci_enable_bus_mastering(const PCIDeviceInfo* device) {
    return pci_enable_bus_mastering(device) ? 0 : -1;
}

extern "C" uint32_t driver_mmio_read32(uint64_t address) {
    return *(volatile uint32_t*)(uintptr_t)address;
}

extern "C" void driver_mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t*)(uintptr_t)address = value;
}

extern "C" int64_t driver_vfs_open(const char* path, uint64_t mode) {
    return vfs_open(path, (uint32_t)mode);
}

extern "C" int64_t driver_vfs_read(uint64_t fd, void* buffer, uint64_t size) {
    uint32_t bytes_read = 0;
    int result = vfs_read((int)fd, (uint8_t*)buffer, (uint32_t)size, &bytes_read);
    if (result != VFS_OK) {
        return result;
    }
    return bytes_read;
}

extern "C" int64_t driver_vfs_write(uint64_t fd, const void* buffer, uint64_t size) {
    uint32_t bytes_written = 0;
    int result = vfs_write((int)fd, (const uint8_t*)buffer, (uint32_t)size, &bytes_written);
    if (result != VFS_OK) {
        return result;
    }
    return bytes_written;
}

extern "C" int64_t driver_vfs_close(uint64_t fd) {
    return vfs_close((int)fd);
}

extern "C" int64_t driver_block_read_sector(uint64_t lba, void* buffer) {
    return ata.read_sector((uint32_t)lba, (uint8_t*)buffer) ? 0 : -1;
}

extern "C" int64_t driver_block_write_sector(uint64_t lba, const void* buffer) {
    return ata.write_sector((uint32_t)lba, (const uint8_t*)buffer) ? 0 : -1;
}

void driver_manager_register_kernel_exports() {
    driver_export_register("kernel", "klog", (void*)driver_klog, 0);
    driver_export_register("kernel", "kmalloc", (void*)driver_kmalloc, 0);
    driver_export_register("kernel", "kfree", (void*)driver_kfree, 0);
    driver_export_register("kernel", "gop_get_info", (void*)driver_gop_get_info, DRV_PERMISSION_DISPLAY);
    driver_export_register("kernel", "gop_clear", (void*)driver_gop_clear, DRV_PERMISSION_DISPLAY);
    driver_export_register("kernel", "gop_putpixel", (void*)driver_gop_putpixel, DRV_PERMISSION_DISPLAY);
    driver_export_register("kernel", "gop_fill_rect", (void*)driver_gop_fill_rect, DRV_PERMISSION_DISPLAY);
    driver_export_register("kernel", "pci_read_config32", (void*)driver_pci_read_config32, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_write_config32", (void*)driver_pci_write_config32, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_device_count", (void*)driver_pci_device_count, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_get_device", (void*)driver_pci_get_device, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_find_device", (void*)driver_pci_find_device, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_get_bar", (void*)driver_pci_get_bar, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_map_bar", (void*)driver_pci_map_bar, DRV_PERMISSION_PCI | DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "pci_enable_memory_space", (void*)driver_pci_enable_memory_space, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_enable_bus_mastering", (void*)driver_pci_enable_bus_mastering, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "mmio_read32", (void*)driver_mmio_read32, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "mmio_write32", (void*)driver_mmio_write32, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "vfs_open", (void*)driver_vfs_open, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_read", (void*)driver_vfs_read, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_write", (void*)driver_vfs_write, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_close", (void*)driver_vfs_close, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "block_read_sector", (void*)driver_block_read_sector, DRV_PERMISSION_BLOCK);
    driver_export_register("kernel", "block_write_sector", (void*)driver_block_write_sector, DRV_PERMISSION_BLOCK);
}
