#ifndef KERNEL_DRIVER_DRIVER_DMA_H
#define KERNEL_DRIVER_DRIVER_DMA_H

#include <stdint.h>
#include "kernel/driver/driver_manager.h"

#define DRIVER_MAX_DMA_DOMAINS 64u
#define DRIVER_MAX_DMA_BUFFERS 64u
#define DRIVER_DMA_ARENA_BASE  0x0000000068000000ULL
#define DRIVER_DMA_ARENA_LIMIT 0x0000000070000000ULL
#define DRIVER_DMA_OWNER_BUDGET (4u * 1024u * 1024u)
#define DRIVER_DMA_GLOBAL_BUDGET (16u * 1024u * 1024u)

#define DRIVER_DMA_POLICY_TRUSTED_DIRECT 1u
#define DRIVER_DMA_POLICY_REQUIRE_ISOLATION 2u
#define DRIVER_DMA_BACKEND_DIRECT 1u

struct DriverDmaAddress { uint64_t value; };
struct DriverDmaDomainHandle { uint32_t slot; uint32_t generation; };
struct DriverDmaHandle { uint32_t slot; uint32_t generation; };

struct DriverDmaBuffer {
    DriverDmaHandle handle;
    void* cpu_address;
    DriverDmaAddress dma_address;
    uint64_t size;
    uint32_t page_count;
    uint32_t reserved;
};

struct DriverDmaStats {
    uint32_t domains;
    uint32_t coherent_buffers;
    uint32_t high_water_buffers;
    uint32_t quarantined_buffers;
    uint64_t coherent_bytes;
    uint64_t peak_bytes;
    uint64_t allocations;
    uint64_t releases;
    uint64_t mask_rejections;
    uint64_t owner_rejections;
    uint64_t stale_rejections;
    uint64_t budget_rejections;
    uint64_t isolation_rejections;
    uint64_t bus_master_rejections;
};

void driver_dma_init();
DriverDmaHandle driver_dma_invalid();
int driver_dma_prepare_device(DriverIdentity owner, DriverDeviceIdentity device,
                              uint32_t policy,
                              DriverDmaDomainHandle* out_domain);
int driver_dma_set_mask(DriverIdentity owner, DriverDeviceIdentity device,
                        uint32_t bits);
int driver_dma_enable_bus_mastering(DriverIdentity owner,
                                    DriverDeviceIdentity device);
int driver_dma_alloc_coherent(DriverIdentity owner,
                              DriverDeviceIdentity device,
                              uint64_t size, uint64_t alignment,
                              uint64_t boundary, DriverDmaBuffer* out);
int driver_dma_free_coherent(DriverIdentity owner, DriverDmaHandle handle);
int driver_dma_prepare_device_current(DriverDeviceIdentity device,
                                      uint32_t policy,
                                      DriverDmaDomainHandle* out_domain);
int driver_dma_set_mask_current(DriverDeviceIdentity device, uint32_t bits);
int driver_dma_enable_bus_mastering_current(DriverDeviceIdentity device);
int driver_dma_alloc_coherent_current(DriverDeviceIdentity device,
                                      uint64_t size, uint64_t alignment,
                                      uint64_t boundary,
                                      DriverDmaBuffer* out);
int driver_dma_free_coherent_current(DriverDmaHandle handle);
uint32_t driver_dma_release_owner(DriverIdentity owner);
uint32_t driver_dma_owner_count(DriverIdentity owner);
void driver_dma_get_stats(DriverDmaStats* out);

#endif
