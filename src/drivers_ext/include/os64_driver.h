#ifndef OS64_DRIVER_H
#define OS64_DRIVER_H

typedef unsigned long long os64_u64;
typedef signed long long os64_i64;
typedef unsigned int os64_u32;

#define OS64_PCI_MAX_BARS 6

typedef struct os64_pci_bar_info {
    os64_u64 base;
    os64_u64 size;
    os64_u32 type;
    os64_u32 flags;
} os64_pci_bar_info;

typedef struct os64_pci_device_info {
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned short command;
    unsigned short status;
    unsigned char bus;
    unsigned char device;
    unsigned char function;
    unsigned char revision_id;
    unsigned char prog_if;
    unsigned char subclass;
    unsigned char class_code;
    unsigned char header_type;
    unsigned char multifunction;
    unsigned char irq_line;
    unsigned char irq_pin;
    unsigned char bar_count;
    unsigned char reserved[3];
    os64_u32 raw_bars[OS64_PCI_MAX_BARS];
} os64_pci_device_info;

typedef void (*os64_klog_fn)(const char* text);
typedef void* (*os64_kmalloc_fn)(os64_u64 size);
typedef void (*os64_kfree_fn)(void* ptr);
typedef os64_u32 (*os64_pci_read_config32_fn)(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset);
typedef void (*os64_pci_write_config32_fn)(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset, os64_u32 value);
typedef os64_u64 (*os64_pci_device_count_fn)(void);
typedef os64_i64 (*os64_pci_get_device_fn)(os64_u64 index, os64_pci_device_info* out);
typedef os64_i64 (*os64_pci_find_device_fn)(os64_u64 vendor_id, os64_u64 device_id, os64_pci_device_info* out);
typedef os64_i64 (*os64_pci_get_bar_fn)(const os64_pci_device_info* device, os64_u64 bar_index, os64_pci_bar_info* out);
typedef os64_i64 (*os64_pci_enable_memory_space_fn)(const os64_pci_device_info* device);
typedef os64_i64 (*os64_pci_enable_bus_mastering_fn)(const os64_pci_device_info* device);
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
extern os64_pci_device_count_fn kernel__pci_device_count;
extern os64_pci_get_device_fn kernel__pci_get_device;
extern os64_pci_find_device_fn kernel__pci_find_device;
extern os64_pci_get_bar_fn kernel__pci_get_bar;
extern os64_pci_enable_memory_space_fn kernel__pci_enable_memory_space;
extern os64_pci_enable_bus_mastering_fn kernel__pci_enable_bus_mastering;
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

static inline os64_u64 os64_pci_device_count(void) {
    return kernel__pci_device_count();
}

static inline os64_i64 os64_pci_get_device(os64_u64 index, os64_pci_device_info* out) {
    return kernel__pci_get_device(index, out);
}

static inline os64_i64 os64_pci_find_device(os64_u64 vendor_id, os64_u64 device_id, os64_pci_device_info* out) {
    return kernel__pci_find_device(vendor_id, device_id, out);
}

static inline os64_i64 os64_pci_get_bar(const os64_pci_device_info* device, os64_u64 bar_index, os64_pci_bar_info* out) {
    return kernel__pci_get_bar(device, bar_index, out);
}

static inline os64_i64 os64_pci_enable_memory_space(const os64_pci_device_info* device) {
    return kernel__pci_enable_memory_space(device);
}

static inline os64_i64 os64_pci_enable_bus_mastering(const os64_pci_device_info* device) {
    return kernel__pci_enable_bus_mastering(device);
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
