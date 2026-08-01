#ifndef KERNEL_DRIVER_DRIVER_MMIO_H
#define KERNEL_DRIVER_DRIVER_MMIO_H

#include <stdint.h>
#include "kernel/driver/driver_manager.h"

#define DRIVER_MAX_MMIO_MAPPINGS 64u
#define DRIVER_MMIO_ARENA_BASE  0x0000000064000000ULL
#define DRIVER_MMIO_ARENA_LIMIT 0x0000000068000000ULL
#define DRIVER_MMIO_CACHE_DEVICE_UC 1u
#define DRIVER_MMIO_CACHE_WRITE_COMBINING 2u
#define DRIVER_MMIO_BARRIER_READ 1u
#define DRIVER_MMIO_BARRIER_WRITE 2u
#define DRIVER_MMIO_BARRIER_FULL 3u

struct DriverMmioHandle {
    uint32_t slot;
    uint32_t generation;
};

struct DriverMmioMapping {
    DriverMmioHandle handle;
    uint64_t length;
};

struct DriverMmioStats {
    uint32_t active;
    uint32_t high_water;
    uint32_t arena_pages;
    uint32_t free_pages;
    uint32_t quarantined_pages;
    uint64_t maps;
    uint64_t unmaps;
    uint64_t shared_maps;
    uint64_t stale_rejections;
    uint64_t owner_rejections;
    uint64_t range_rejections;
    uint64_t cache_rejections;
};

void driver_mmio_init();
DriverMmioHandle driver_mmio_invalid();
int driver_mmio_map(DriverIdentity owner, DriverDeviceIdentity device,
                    uint32_t bar_index, uint64_t offset, uint64_t length,
                    uint32_t cache_policy, DriverMmioMapping* out);
int driver_mmio_map_current(DriverDeviceIdentity device, uint32_t bar_index,
                            uint64_t offset, uint64_t length,
                            uint32_t cache_policy, DriverMmioMapping* out);
int driver_mmio_unmap(DriverIdentity owner, DriverMmioHandle handle);
int driver_mmio_unmap_current(DriverMmioHandle handle);
int driver_mmio_read(DriverIdentity owner, DriverMmioHandle handle,
                     uint64_t offset, uint32_t width, uint64_t* out);
int driver_mmio_write(DriverIdentity owner, DriverMmioHandle handle,
                      uint64_t offset, uint32_t width, uint64_t value);
int driver_mmio_read_current(DriverMmioHandle handle, uint64_t offset,
                             uint32_t width, uint64_t* out);
int driver_mmio_write_current(DriverMmioHandle handle, uint64_t offset,
                              uint32_t width, uint64_t value);
int driver_mmio_barrier_current(DriverMmioHandle handle, uint32_t direction);
uint32_t driver_mmio_release_owner(DriverIdentity owner);
uint32_t driver_mmio_owner_count(DriverIdentity owner);
void driver_mmio_get_stats(DriverMmioStats* out);

#endif
