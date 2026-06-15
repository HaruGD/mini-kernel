#include "kernel/driver/driver_manager.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/kernel_exports.h"
#include "kernel/kutil64.h"
#include "arch/x86/io.h"
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

static uint32_t pci_config_address(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset) {
    return 0x80000000U |
           (((uint32_t)bus & 0xFFU) << 16) |
           (((uint32_t)device & 0x1FU) << 11) |
           (((uint32_t)function & 0x07U) << 8) |
           ((uint32_t)offset & 0xFCU);
}

extern "C" uint32_t driver_pci_read_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset) {
    outl(0xCF8, pci_config_address(bus, device, function, offset));
    return inl(0xCFC);
}

extern "C" void driver_pci_write_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset, uint32_t value) {
    outl(0xCF8, pci_config_address(bus, device, function, offset));
    outl(0xCFC, value);
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
    driver_export_register("kernel", "pci_read_config32", (void*)driver_pci_read_config32, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_write_config32", (void*)driver_pci_write_config32, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "mmio_read32", (void*)driver_mmio_read32, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "mmio_write32", (void*)driver_mmio_write32, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "vfs_open", (void*)driver_vfs_open, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_read", (void*)driver_vfs_read, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_write", (void*)driver_vfs_write, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_close", (void*)driver_vfs_close, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "block_read_sector", (void*)driver_block_read_sector, DRV_PERMISSION_BLOCK);
    driver_export_register("kernel", "block_write_sector", (void*)driver_block_write_sector, DRV_PERMISSION_BLOCK);
}
