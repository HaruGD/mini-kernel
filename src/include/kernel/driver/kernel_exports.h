#ifndef KERNEL_DRIVER_KERNEL_EXPORTS_H
#define KERNEL_DRIVER_KERNEL_EXPORTS_H

#include <stdint.h>

extern "C" void driver_klog(const char* text);
extern "C" void* driver_kmalloc(uint64_t size);
extern "C" void driver_kfree(void* ptr);
extern "C" uint32_t driver_pci_read_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset);
extern "C" void driver_pci_write_config32(uint64_t bus, uint64_t device, uint64_t function, uint64_t offset, uint32_t value);
extern "C" uint32_t driver_mmio_read32(uint64_t address);
extern "C" void driver_mmio_write32(uint64_t address, uint32_t value);
extern "C" int64_t driver_vfs_open(const char* path, uint64_t mode);
extern "C" int64_t driver_vfs_read(uint64_t fd, void* buffer, uint64_t size);
extern "C" int64_t driver_vfs_write(uint64_t fd, const void* buffer, uint64_t size);
extern "C" int64_t driver_vfs_close(uint64_t fd);
extern "C" int64_t driver_block_read_sector(uint64_t lba, void* buffer);
extern "C" int64_t driver_block_write_sector(uint64_t lba, const void* buffer);

#endif
