#ifndef OS64_DRIVER_H
#define OS64_DRIVER_H

typedef unsigned long long os64_u64;
typedef signed long long os64_i64;
typedef unsigned int os64_u32;

typedef void (*os64_klog_fn)(const char* text);
typedef void* (*os64_kmalloc_fn)(os64_u64 size);
typedef void (*os64_kfree_fn)(void* ptr);
typedef os64_u32 (*os64_pci_read_config32_fn)(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset);
typedef void (*os64_pci_write_config32_fn)(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset, os64_u32 value);
typedef os64_u32 (*os64_mmio_read32_fn)(os64_u64 address);
typedef void (*os64_mmio_write32_fn)(os64_u64 address, os64_u32 value);
typedef os64_i64 (*os64_vfs_open_fn)(const char* path, os64_u64 mode);
typedef os64_i64 (*os64_vfs_read_fn)(os64_u64 fd, void* buffer, os64_u64 size);
typedef os64_i64 (*os64_vfs_write_fn)(os64_u64 fd, const void* buffer, os64_u64 size);
typedef os64_i64 (*os64_vfs_close_fn)(os64_u64 fd);
typedef os64_i64 (*os64_block_read_sector_fn)(os64_u64 lba, void* buffer);
typedef os64_i64 (*os64_block_write_sector_fn)(os64_u64 lba, const void* buffer);

extern os64_klog_fn kernel__klog;
extern os64_kmalloc_fn kernel__kmalloc;
extern os64_kfree_fn kernel__kfree;
extern os64_pci_read_config32_fn kernel__pci_read_config32;
extern os64_pci_write_config32_fn kernel__pci_write_config32;
extern os64_mmio_read32_fn kernel__mmio_read32;
extern os64_mmio_write32_fn kernel__mmio_write32;
extern os64_vfs_open_fn kernel__vfs_open;
extern os64_vfs_read_fn kernel__vfs_read;
extern os64_vfs_write_fn kernel__vfs_write;
extern os64_vfs_close_fn kernel__vfs_close;
extern os64_block_read_sector_fn kernel__block_read_sector;
extern os64_block_write_sector_fn kernel__block_write_sector;

#define OS64_EXPORT __attribute__((used))

static inline void os64_klog(const char* text) {
    kernel__klog(text);
}

static inline void* os64_kmalloc(os64_u64 size) {
    return kernel__kmalloc(size);
}

static inline void os64_kfree(void* ptr) {
    kernel__kfree(ptr);
}

static inline os64_u32 os64_pci_read_config32(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset) {
    return kernel__pci_read_config32(bus, device, function, offset);
}

static inline void os64_pci_write_config32(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset, os64_u32 value) {
    kernel__pci_write_config32(bus, device, function, offset, value);
}

static inline os64_u32 os64_mmio_read32(os64_u64 address) {
    return kernel__mmio_read32(address);
}

static inline void os64_mmio_write32(os64_u64 address, os64_u32 value) {
    kernel__mmio_write32(address, value);
}

static inline os64_i64 os64_vfs_open(const char* path, os64_u64 mode) {
    return kernel__vfs_open(path, mode);
}

static inline os64_i64 os64_vfs_read(os64_u64 fd, void* buffer, os64_u64 size) {
    return kernel__vfs_read(fd, buffer, size);
}

static inline os64_i64 os64_vfs_write(os64_u64 fd, const void* buffer, os64_u64 size) {
    return kernel__vfs_write(fd, buffer, size);
}

static inline os64_i64 os64_vfs_close(os64_u64 fd) {
    return kernel__vfs_close(fd);
}

static inline os64_i64 os64_block_read_sector(os64_u64 lba, void* buffer) {
    return kernel__block_read_sector(lba, buffer);
}

static inline os64_i64 os64_block_write_sector(os64_u64 lba, const void* buffer) {
    return kernel__block_write_sector(lba, buffer);
}

#endif
