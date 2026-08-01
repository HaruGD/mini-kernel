#include "kernel/driver/driver_manager.h"
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/driver_mmio.h"
#include "kernel/driver/driver_dma.h"
#include "kernel/driver/drv_format.h"
#include "kernel/driver/kernel_exports.h"
#include "kernel/kutil64.h"
#include "drivers/gop.h"
#include "kernel/pci.h"
#include "drivers/ata.h"
#include "fs/vfs.h"
#include <stddef.h>

extern "C" {
    #include "kernel/mm/heap.h"
}

extern ATADriver ata;

extern "C" void driver_klog(const char* text) {
    if (!driver_execution_runtime_allowed()) return;
    print("\n[drv] ");
    print(text != 0 ? text : "(null)");
    print("\n");
}

extern "C" void* driver_kmalloc(uint64_t size) {
    if (!driver_execution_require_sleepable()) return 0;
    return kmalloc((size_t)size);
}

extern "C" void driver_kfree(void* ptr) {
    if (!driver_execution_require_sleepable()) return;
    kfree(ptr);
}

extern "C" int64_t driver_owned_alloc(uint64_t size, uint64_t alignment,
                                        uint64_t flags, const char* tag,
                                        DriverAllocationResult* out) {
    return driver_allocation_create_current(size, alignment, (uint32_t)flags,
                                            tag, out);
}

extern "C" int64_t driver_owned_free(DriverAllocationHandle handle) {
    return driver_allocation_release_current(handle);
}

extern "C" const GOPInfo* driver_gop_get_info() {
    if (!driver_execution_require_sleepable()) return 0;
    return gop.info();
}

extern "C" void driver_gop_clear(uint32_t color) {
    if (!driver_execution_require_sleepable()) return;
    gop.clear(color);
}

extern "C" void driver_gop_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!driver_execution_require_sleepable()) return;
    gop.putpixel(x, y, color);
}

extern "C" void driver_gop_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!driver_execution_require_sleepable()) return;
    gop.fill_rect(x, y, width, height, color);
}

extern "C" uint32_t driver_pci_read_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset) {
    if (!driver_execution_require_sleepable()) return 0xFFFFFFFFu;
    return pci_read_config32(bus, device, function, offset);
}

extern "C" void driver_pci_write_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset, uint32_t value) {
    if (!driver_execution_require_sleepable()) return;
    pci_write_config32(bus, device, function, offset, value);
}

extern "C" uint64_t driver_pci_device_count() {
    if (!driver_execution_require_sleepable()) return 0;
    return pci_get_device_count();
}

extern "C" int64_t driver_pci_get_device(uint64_t index, PCIDeviceInfo* out) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    const PCIDeviceInfo* device = pci_get_device((uint32_t)index);
    if (device == 0 || out == 0) {
        return -1;
    }
    *out = *device;
    return 0;
}

extern "C" int64_t driver_pci_find_device(uint64_t vendor_id, uint64_t device_id, PCIDeviceInfo* out) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return pci_find_device((uint16_t)vendor_id, (uint16_t)device_id, out) ? 0 : -1;
}

extern "C" int64_t driver_pci_get_bar(const PCIDeviceInfo* device, uint64_t bar_index, PCIBarInfo* out) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return pci_get_bar(device, (uint32_t)bar_index, out) ? 0 : -1;
}

extern "C" void* driver_pci_map_bar(const PCIDeviceInfo* device, uint64_t bar_index, PCIBarInfo* out) {
    if (!driver_execution_require_sleepable()) return 0;
    return pci_map_bar(device, (uint32_t)bar_index, out);
}

extern "C" int64_t driver_pci_enable_memory_space(const PCIDeviceInfo* device) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return pci_enable_memory_space(device) ? 0 : -1;
}

extern "C" int64_t driver_pci_enable_bus_mastering(const PCIDeviceInfo* device) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return pci_enable_bus_mastering(device) ? 0 : -1;
}

extern "C" int64_t driver_pci_bind_device(const PCIDeviceInfo* device, uint64_t flags) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    const char* driver_name = driver_manager_current_lifecycle_driver();
    if (driver_name == 0) {
        return DRIVER_LOAD_BIND_DENIED;
    }
    return driver_manager_bind_pci(driver_name, device, (uint32_t)flags);
}

extern "C" int64_t driver_pci_bind_device_handle(
    const PCIDeviceInfo* device, uint64_t flags, DriverDeviceIdentity* out) {
    if (out != 0) *out = driver_device_identity_invalid();
    if (!driver_execution_require_sleepable() || out == 0)
        return DRIVER_LOAD_CONTEXT_DENIED;
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    const char* driver_name = driver_manager_current_lifecycle_driver();
    if (driver_name == 0) return DRIVER_LOAD_BIND_DENIED;
    int result = driver_manager_bind_pci(driver_name, device, (uint32_t)flags);
    if (result != DRIVER_LOAD_OK) return result;
    return driver_manager_bound_pci_identity(context.owner, device, out)
        ? DRIVER_LOAD_OK : DRIVER_LOAD_BIND_DENIED;
}

extern "C" int64_t driver_pci_map_bar_handle(
    DriverDeviceIdentity device, uint64_t bar_index, uint64_t offset,
    uint64_t length, uint64_t cache_policy, DriverMmioMapping* out) {
    return driver_mmio_map_current(device, (uint32_t)bar_index, offset, length,
                                   (uint32_t)cache_policy, out);
}

extern "C" int64_t driver_pci_unmap_bar_handle(DriverMmioHandle handle) {
    return driver_mmio_unmap_current(handle);
}

extern "C" int64_t driver_mmio_read_handle(DriverMmioHandle handle,
                                              uint64_t offset,
                                              uint64_t width,
                                              uint64_t* out) {
    return driver_mmio_read_current(handle, offset, (uint32_t)width, out);
}

extern "C" int64_t driver_mmio_write_handle(DriverMmioHandle handle,
                                               uint64_t offset,
                                               uint64_t width,
                                               uint64_t value) {
    return driver_mmio_write_current(handle, offset, (uint32_t)width, value);
}

extern "C" int64_t driver_mmio_barrier_handle(DriverMmioHandle handle,
                                                 uint64_t direction) {
    return driver_mmio_barrier_current(handle, (uint32_t)direction);
}

extern "C" int64_t driver_dma_prepare_device_handle(
    DriverDeviceIdentity device, uint64_t policy, DriverDmaDomainHandle* out) {
    return driver_dma_prepare_device_current(device, (uint32_t)policy, out);
}

extern "C" int64_t driver_dma_set_mask_handle(DriverDeviceIdentity device,
                                                uint64_t bits) {
    return driver_dma_set_mask_current(device, (uint32_t)bits);
}

extern "C" int64_t driver_dma_enable_bus_mastering_handle(
    DriverDeviceIdentity device) {
    return driver_dma_enable_bus_mastering_current(device);
}

extern "C" int64_t driver_dma_alloc_coherent_handle(
    DriverDeviceIdentity device, uint64_t size, uint64_t alignment,
    uint64_t boundary, DriverDmaBuffer* out) {
    return driver_dma_alloc_coherent_current(device, size, alignment,
                                             boundary, out);
}

extern "C" int64_t driver_dma_free_coherent_handle(DriverDmaHandle handle) {
    return driver_dma_free_coherent_current(handle);
}

extern "C" int64_t driver_dma_map_buffer_handle(
    DriverDeviceIdentity device, DriverAllocationHandle allocation,
    uint64_t offset, uint64_t length, uint64_t direction,
    DriverDmaMapping* out) {
    return driver_dma_map_buffer_current(device, allocation, offset, length,
                                         (uint32_t)direction, out);
}

extern "C" int64_t driver_dma_map_sg_handle(
    DriverDeviceIdentity device, const DriverDmaSource* sources,
    uint64_t source_count, uint64_t direction, DriverDmaMapping* out) {
    if (source_count > UINT32_MAX) return DRIVER_LOAD_BAD_HEADER;
    return driver_dma_map_sg_current(device, sources, (uint32_t)source_count,
                                     (uint32_t)direction, out);
}

extern "C" int64_t driver_dma_sync_for_cpu_handle(
    DriverDmaMappingHandle handle) {
    return driver_dma_sync_for_cpu_current(handle);
}

extern "C" int64_t driver_dma_sync_for_device_handle(
    DriverDmaMappingHandle handle) {
    return driver_dma_sync_for_device_current(handle);
}

extern "C" int64_t driver_dma_unmap_handle(DriverDmaMappingHandle handle) {
    return driver_dma_unmap_current(handle);
}

extern "C" int64_t driver_irq_register(uint64_t irq, DriverIrqHandler handler) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    const char* driver_name = driver_manager_current_lifecycle_driver();
    if (driver_name == 0) {
        return DRIVER_LOAD_IRQ_DENIED;
    }
    return driver_irq_register_handler(driver_name, (uint32_t)irq, handler, 0);
}

extern "C" int64_t driver_irq_unregister(uint64_t irq, DriverIrqHandler handler) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    const char* driver_name = driver_manager_current_lifecycle_driver();
    if (driver_name == 0) {
        return DRIVER_LOAD_IRQ_DENIED;
    }
    return driver_irq_unregister_handler(driver_name, (uint32_t)irq, handler);
}

extern "C" uint32_t driver_mmio_read32(uint64_t address) {
    if (!driver_execution_runtime_allowed()) return 0;
    return *(volatile uint32_t*)(uintptr_t)address;
}

extern "C" void driver_mmio_write32(uint64_t address, uint32_t value) {
    if (!driver_execution_runtime_allowed()) return;
    *(volatile uint32_t*)(uintptr_t)address = value;
}

extern "C" int64_t driver_vfs_open(const char* path, uint64_t mode) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return vfs_open(path, (uint32_t)mode);
}

extern "C" int64_t driver_vfs_read(uint64_t fd, void* buffer, uint64_t size) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    uint32_t bytes_read = 0;
    int result = vfs_read((int)fd, (uint8_t*)buffer, (uint32_t)size, &bytes_read);
    if (result != VFS_OK) {
        return result;
    }
    return bytes_read;
}

extern "C" int64_t driver_vfs_write(uint64_t fd, const void* buffer, uint64_t size) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    uint32_t bytes_written = 0;
    int result = vfs_write((int)fd, (const uint8_t*)buffer, (uint32_t)size, &bytes_written);
    if (result != VFS_OK) {
        return result;
    }
    return bytes_written;
}

extern "C" int64_t driver_vfs_close(uint64_t fd) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return vfs_close((int)fd);
}

extern "C" int64_t driver_block_read_sector(uint64_t lba, void* buffer) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return ata.read_sector((uint32_t)lba, (uint8_t*)buffer) ? 0 : -1;
}

extern "C" int64_t driver_block_write_sector(uint64_t lba, const void* buffer) {
    if (!driver_execution_require_sleepable()) return DRIVER_LOAD_CONTEXT_DENIED;
    return ata.write_sector((uint32_t)lba, (const uint8_t*)buffer) ? 0 : -1;
}

void driver_manager_register_kernel_exports() {
    driver_export_register("kernel", "klog", (void*)driver_klog, 0);
    driver_export_register("kernel", "kmalloc", (void*)driver_kmalloc, 0);
    driver_export_register("kernel", "kfree", (void*)driver_kfree, 0);
    driver_export_register("kernel", "drv_alloc", (void*)driver_owned_alloc, 0);
    driver_export_register("kernel", "drv_free", (void*)driver_owned_free, 0);
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
    driver_export_register("kernel", "pci_enable_memory_space", (void*)driver_pci_enable_memory_space, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_bind_device", (void*)driver_pci_bind_device, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_bind_device_handle", (void*)driver_pci_bind_device_handle, DRV_PERMISSION_PCI);
    driver_export_register("kernel", "pci_map_bar_handle", (void*)driver_pci_map_bar_handle, DRV_PERMISSION_PCI | DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "pci_unmap_bar_handle", (void*)driver_pci_unmap_bar_handle, DRV_PERMISSION_PCI | DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "mmio_read_handle", (void*)driver_mmio_read_handle, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "mmio_write_handle", (void*)driver_mmio_write_handle, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "mmio_barrier_handle", (void*)driver_mmio_barrier_handle, DRV_PERMISSION_MMIO);
    driver_export_register("kernel", "dma_prepare_device", (void*)driver_dma_prepare_device_handle, DRV_PERMISSION_PCI | DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_set_mask", (void*)driver_dma_set_mask_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_enable_bus_mastering", (void*)driver_dma_enable_bus_mastering_handle, DRV_PERMISSION_PCI | DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_alloc_coherent", (void*)driver_dma_alloc_coherent_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_free_coherent", (void*)driver_dma_free_coherent_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_map_buffer", (void*)driver_dma_map_buffer_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_map_sg", (void*)driver_dma_map_sg_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_sync_for_cpu", (void*)driver_dma_sync_for_cpu_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_sync_for_device", (void*)driver_dma_sync_for_device_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "dma_unmap", (void*)driver_dma_unmap_handle, DRV_PERMISSION_DMA);
    driver_export_register("kernel", "irq_register", (void*)driver_irq_register, DRV_PERMISSION_INTERRUPT);
    driver_export_register("kernel", "irq_unregister", (void*)driver_irq_unregister, DRV_PERMISSION_INTERRUPT);
    driver_export_register("kernel", "vfs_open", (void*)driver_vfs_open, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_read", (void*)driver_vfs_read, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_write", (void*)driver_vfs_write, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "vfs_close", (void*)driver_vfs_close, DRV_PERMISSION_VFS);
    driver_export_register("kernel", "block_read_sector", (void*)driver_block_read_sector, DRV_PERMISSION_BLOCK);
    driver_export_register("kernel", "block_write_sector", (void*)driver_block_write_sector, DRV_PERMISSION_BLOCK);
}
