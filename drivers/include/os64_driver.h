#ifndef OS64_DRIVER_H
#define OS64_DRIVER_H

typedef unsigned long long os64_u64;
typedef signed long long os64_i64;
typedef unsigned int os64_u32;

#define OS64_DRV_ALLOC_ZERO   0x01u
#define OS64_DRV_ALLOC_PAGES  0x02u
#define OS64_DRV_ALLOC_ATOMIC 0x04u

typedef struct os64_driver_allocation_handle {
    os64_u32 slot;
    os64_u32 generation;
} os64_driver_allocation_handle;

typedef struct os64_driver_allocation {
    os64_driver_allocation_handle handle;
    void* address;
    os64_u64 size;
} os64_driver_allocation;

typedef struct os64_driver_device_handle {
    os64_u32 slot;
    os64_u32 generation;
} os64_driver_device_handle;

typedef struct os64_driver_mmio_handle {
    os64_u32 slot;
    os64_u32 generation;
} os64_driver_mmio_handle;

typedef struct os64_driver_mmio_mapping {
    os64_driver_mmio_handle handle;
    os64_u64 length;
} os64_driver_mmio_mapping;

typedef struct os64_driver_dma_domain_handle {
    os64_u32 slot;
    os64_u32 generation;
} os64_driver_dma_domain_handle;

typedef struct os64_driver_dma_handle {
    os64_u32 slot;
    os64_u32 generation;
} os64_driver_dma_handle;

typedef struct os64_driver_dma_address { os64_u64 value; } os64_driver_dma_address;

typedef struct os64_driver_dma_buffer {
    os64_driver_dma_handle handle;
    void* cpu_address;
    os64_driver_dma_address dma_address;
    os64_u64 size;
    os64_u32 page_count;
    os64_u32 reserved;
} os64_driver_dma_buffer;

#define OS64_DMA_TRUSTED_DIRECT 1u
#define OS64_DMA_REQUIRE_ISOLATION 2u
#define OS64_DMA_TO_DEVICE 1u
#define OS64_DMA_FROM_DEVICE 2u
#define OS64_DMA_BIDIRECTIONAL 3u
#define OS64_DMA_MAX_SOURCES 16u
#define OS64_DMA_MAX_SEGMENTS 32u

typedef struct os64_driver_dma_mapping_handle {
    os64_u32 slot;
    os64_u32 generation;
} os64_driver_dma_mapping_handle;

typedef struct os64_driver_dma_source {
    os64_driver_allocation_handle allocation;
    os64_u64 offset;
    os64_u64 length;
} os64_driver_dma_source;

typedef struct os64_driver_dma_segment {
    os64_driver_dma_address address;
    os64_u64 length;
} os64_driver_dma_segment;

typedef struct os64_driver_dma_mapping {
    os64_driver_dma_mapping_handle handle;
    os64_u32 segment_count;
    os64_u32 direction;
    os64_driver_dma_segment segments[OS64_DMA_MAX_SEGMENTS];
} os64_driver_dma_mapping;

#define OS64_MMIO_DEVICE_UC 1u
#define OS64_MMIO_WRITE_COMBINING 2u
#define OS64_MMIO_BARRIER_READ 1u
#define OS64_MMIO_BARRIER_WRITE 2u
#define OS64_MMIO_BARRIER_FULL 3u

#define OS64_PCI_MAX_BARS 6

typedef struct os64_gop_info {
    os64_u64 framebuffer_addr;
    os64_u64 framebuffer_size;
    os64_u32 width;
    os64_u32 height;
    os64_u32 pixels_per_scanline;
    os64_u32 format;
} os64_gop_info;

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
typedef os64_i64 (*os64_drv_alloc_fn)(os64_u64 size, os64_u64 alignment,
                                      os64_u64 flags, const char* tag,
                                      os64_driver_allocation* out);
typedef os64_i64 (*os64_drv_free_fn)(os64_driver_allocation_handle handle);
typedef const os64_gop_info* (*os64_gop_get_info_fn)(void);
typedef void (*os64_gop_clear_fn)(os64_u32 color);
typedef void (*os64_gop_putpixel_fn)(os64_u32 x, os64_u32 y, os64_u32 color);
typedef void (*os64_gop_fill_rect_fn)(os64_u32 x, os64_u32 y, os64_u32 width, os64_u32 height, os64_u32 color);
typedef os64_u32 (*os64_pci_read_config32_fn)(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset);
typedef void (*os64_pci_write_config32_fn)(os64_u64 bus, os64_u64 device, os64_u64 function, os64_u64 offset, os64_u32 value);
typedef os64_u64 (*os64_pci_device_count_fn)(void);
typedef os64_i64 (*os64_pci_get_device_fn)(os64_u64 index, os64_pci_device_info* out);
typedef os64_i64 (*os64_pci_find_device_fn)(os64_u64 vendor_id, os64_u64 device_id, os64_pci_device_info* out);
typedef os64_i64 (*os64_pci_get_bar_fn)(const os64_pci_device_info* device, os64_u64 bar_index, os64_pci_bar_info* out);
typedef os64_i64 (*os64_pci_enable_memory_space_fn)(const os64_pci_device_info* device);
typedef os64_i64 (*os64_pci_bind_device_fn)(const os64_pci_device_info* device, os64_u64 flags);
typedef os64_i64 (*os64_pci_bind_device_handle_fn)(const os64_pci_device_info* device, os64_u64 flags, os64_driver_device_handle* out);
typedef os64_i64 (*os64_pci_map_bar_handle_fn)(os64_driver_device_handle device, os64_u64 bar_index, os64_u64 offset, os64_u64 length, os64_u64 cache_policy, os64_driver_mmio_mapping* out);
typedef os64_i64 (*os64_pci_unmap_bar_handle_fn)(os64_driver_mmio_handle handle);
typedef os64_i64 (*os64_mmio_read_handle_fn)(os64_driver_mmio_handle handle, os64_u64 offset, os64_u64 width, os64_u64* out);
typedef os64_i64 (*os64_mmio_write_handle_fn)(os64_driver_mmio_handle handle, os64_u64 offset, os64_u64 width, os64_u64 value);
typedef os64_i64 (*os64_mmio_barrier_handle_fn)(os64_driver_mmio_handle handle, os64_u64 direction);
typedef os64_i64 (*os64_dma_prepare_device_fn)(os64_driver_device_handle device, os64_u64 policy, os64_driver_dma_domain_handle* out);
typedef os64_i64 (*os64_dma_set_mask_fn)(os64_driver_device_handle device, os64_u64 bits);
typedef os64_i64 (*os64_dma_enable_bus_mastering_fn)(os64_driver_device_handle device);
typedef os64_i64 (*os64_dma_alloc_coherent_fn)(os64_driver_device_handle device, os64_u64 size, os64_u64 alignment, os64_u64 boundary, os64_driver_dma_buffer* out);
typedef os64_i64 (*os64_dma_free_coherent_fn)(os64_driver_dma_handle handle);
typedef os64_i64 (*os64_dma_map_buffer_fn)(os64_driver_device_handle device, os64_driver_allocation_handle allocation, os64_u64 offset, os64_u64 length, os64_u64 direction, os64_driver_dma_mapping* out);
typedef os64_i64 (*os64_dma_map_sg_fn)(os64_driver_device_handle device, const os64_driver_dma_source* sources, os64_u64 source_count, os64_u64 direction, os64_driver_dma_mapping* out);
typedef os64_i64 (*os64_dma_sync_fn)(os64_driver_dma_mapping_handle handle);
typedef os64_i64 (*os64_dma_unmap_fn)(os64_driver_dma_mapping_handle handle);
typedef os64_u64 (*os64_irq_handler_fn)(os64_u64 irq);
typedef os64_i64 (*os64_irq_register_fn)(os64_u64 irq, os64_irq_handler_fn handler);
typedef os64_i64 (*os64_irq_unregister_fn)(os64_u64 irq, os64_irq_handler_fn handler);
typedef os64_i64 (*os64_vfs_open_fn)(const char* path, os64_u64 mode);
typedef os64_i64 (*os64_vfs_read_fn)(os64_u64 fd, void* buffer, os64_u64 size);
typedef os64_i64 (*os64_vfs_write_fn)(os64_u64 fd, const void* buffer, os64_u64 size);
typedef os64_i64 (*os64_vfs_close_fn)(os64_u64 fd);
typedef os64_i64 (*os64_block_read_sector_fn)(os64_u64 lba, void* buffer);
typedef os64_i64 (*os64_block_write_sector_fn)(os64_u64 lba, const void* buffer);

#ifdef __cplusplus
extern "C" {
#endif

extern os64_klog_fn kernel__klog;
extern os64_kmalloc_fn kernel__kmalloc;
extern os64_kfree_fn kernel__kfree;
extern os64_drv_alloc_fn kernel__drv_alloc;
extern os64_drv_free_fn kernel__drv_free;
extern os64_gop_get_info_fn kernel__gop_get_info;
extern os64_gop_clear_fn kernel__gop_clear;
extern os64_gop_putpixel_fn kernel__gop_putpixel;
extern os64_gop_fill_rect_fn kernel__gop_fill_rect;
extern os64_pci_read_config32_fn kernel__pci_read_config32;
extern os64_pci_write_config32_fn kernel__pci_write_config32;
extern os64_pci_device_count_fn kernel__pci_device_count;
extern os64_pci_get_device_fn kernel__pci_get_device;
extern os64_pci_find_device_fn kernel__pci_find_device;
extern os64_pci_get_bar_fn kernel__pci_get_bar;
extern os64_pci_enable_memory_space_fn kernel__pci_enable_memory_space;
extern os64_pci_bind_device_fn kernel__pci_bind_device;
extern os64_pci_bind_device_handle_fn kernel__pci_bind_device_handle;
extern os64_pci_map_bar_handle_fn kernel__pci_map_bar_handle;
extern os64_pci_unmap_bar_handle_fn kernel__pci_unmap_bar_handle;
extern os64_mmio_read_handle_fn kernel__mmio_read_handle;
extern os64_mmio_write_handle_fn kernel__mmio_write_handle;
extern os64_mmio_barrier_handle_fn kernel__mmio_barrier_handle;
extern os64_dma_prepare_device_fn kernel__dma_prepare_device;
extern os64_dma_set_mask_fn kernel__dma_set_mask;
extern os64_dma_enable_bus_mastering_fn kernel__dma_enable_bus_mastering;
extern os64_dma_alloc_coherent_fn kernel__dma_alloc_coherent;
extern os64_dma_free_coherent_fn kernel__dma_free_coherent;
extern os64_dma_map_buffer_fn kernel__dma_map_buffer;
extern os64_dma_map_sg_fn kernel__dma_map_sg;
extern os64_dma_sync_fn kernel__dma_sync_for_cpu;
extern os64_dma_sync_fn kernel__dma_sync_for_device;
extern os64_dma_unmap_fn kernel__dma_unmap;
extern os64_irq_register_fn kernel__irq_register;
extern os64_irq_unregister_fn kernel__irq_unregister;
extern os64_vfs_open_fn kernel__vfs_open;
extern os64_vfs_read_fn kernel__vfs_read;
extern os64_vfs_write_fn kernel__vfs_write;
extern os64_vfs_close_fn kernel__vfs_close;
extern os64_block_read_sector_fn kernel__block_read_sector;
extern os64_block_write_sector_fn kernel__block_write_sector;

#ifdef __cplusplus
}
#endif

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

static inline os64_i64 os64_drv_alloc(os64_u64 size, os64_u64 alignment,
                                      os64_u64 flags, const char* tag,
                                      os64_driver_allocation* out) {
    return kernel__drv_alloc(size, alignment, flags, tag, out);
}

static inline os64_i64 os64_drv_free(os64_driver_allocation_handle handle) {
    return kernel__drv_free(handle);
}

static inline const os64_gop_info* os64_gop_get_info(void) {
    return kernel__gop_get_info();
}

static inline void os64_gop_clear(os64_u32 color) {
    kernel__gop_clear(color);
}

static inline void os64_gop_putpixel(os64_u32 x, os64_u32 y, os64_u32 color) {
    kernel__gop_putpixel(x, y, color);
}

static inline void os64_gop_fill_rect(os64_u32 x, os64_u32 y, os64_u32 width, os64_u32 height, os64_u32 color) {
    kernel__gop_fill_rect(x, y, width, height, color);
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

static inline os64_i64 os64_pci_bind_device(const os64_pci_device_info* device, os64_u64 flags) {
    return kernel__pci_bind_device(device, flags);
}

static inline os64_i64 os64_pci_bind_device_handle(const os64_pci_device_info* device, os64_u64 flags, os64_driver_device_handle* out) {
    return kernel__pci_bind_device_handle(device, flags, out);
}

static inline os64_i64 os64_pci_map_bar_handle(os64_driver_device_handle device, os64_u64 bar_index, os64_u64 offset, os64_u64 length, os64_u64 cache_policy, os64_driver_mmio_mapping* out) {
    return kernel__pci_map_bar_handle(device, bar_index, offset, length, cache_policy, out);
}

static inline os64_i64 os64_pci_unmap_bar_handle(os64_driver_mmio_handle handle) {
    return kernel__pci_unmap_bar_handle(handle);
}

static inline os64_i64 os64_mmio_read(os64_driver_mmio_handle handle, os64_u64 offset, os64_u64 width, os64_u64* out) {
    return kernel__mmio_read_handle(handle, offset, width, out);
}

static inline os64_i64 os64_mmio_write(os64_driver_mmio_handle handle, os64_u64 offset, os64_u64 width, os64_u64 value) {
    return kernel__mmio_write_handle(handle, offset, width, value);
}

static inline os64_i64 os64_mmio_barrier(os64_driver_mmio_handle handle, os64_u64 direction) {
    return kernel__mmio_barrier_handle(handle, direction);
}

static inline os64_i64 os64_dma_prepare_device(os64_driver_device_handle device, os64_u64 policy, os64_driver_dma_domain_handle* out) {
    return kernel__dma_prepare_device(device, policy, out);
}

static inline os64_i64 os64_dma_set_mask(os64_driver_device_handle device, os64_u64 bits) {
    return kernel__dma_set_mask(device, bits);
}

static inline os64_i64 os64_dma_enable_bus_mastering(os64_driver_device_handle device) {
    return kernel__dma_enable_bus_mastering(device);
}

static inline os64_i64 os64_dma_alloc_coherent(os64_driver_device_handle device, os64_u64 size, os64_u64 alignment, os64_u64 boundary, os64_driver_dma_buffer* out) {
    return kernel__dma_alloc_coherent(device, size, alignment, boundary, out);
}

static inline os64_i64 os64_dma_free_coherent(os64_driver_dma_handle handle) {
    return kernel__dma_free_coherent(handle);
}

static inline os64_i64 os64_dma_map_buffer(os64_driver_device_handle device, os64_driver_allocation_handle allocation, os64_u64 offset, os64_u64 length, os64_u64 direction, os64_driver_dma_mapping* out) {
    return kernel__dma_map_buffer(device, allocation, offset, length, direction, out);
}

static inline os64_i64 os64_dma_map_sg(os64_driver_device_handle device, const os64_driver_dma_source* sources, os64_u64 source_count, os64_u64 direction, os64_driver_dma_mapping* out) {
    return kernel__dma_map_sg(device, sources, source_count, direction, out);
}

static inline os64_i64 os64_dma_sync_for_cpu(os64_driver_dma_mapping_handle handle) {
    return kernel__dma_sync_for_cpu(handle);
}

static inline os64_i64 os64_dma_sync_for_device(os64_driver_dma_mapping_handle handle) {
    return kernel__dma_sync_for_device(handle);
}

static inline os64_i64 os64_dma_unmap(os64_driver_dma_mapping_handle handle) {
    return kernel__dma_unmap(handle);
}

static inline os64_i64 os64_irq_register(os64_u64 irq, os64_irq_handler_fn handler) {
    return kernel__irq_register(irq, handler);
}

static inline os64_i64 os64_irq_unregister(os64_u64 irq, os64_irq_handler_fn handler) {
    return kernel__irq_unregister(irq, handler);
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
