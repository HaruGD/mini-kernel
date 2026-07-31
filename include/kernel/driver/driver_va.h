#ifndef KERNEL_DRIVER_DRIVER_VA_H
#define KERNEL_DRIVER_DRIVER_VA_H

#include <stdint.h>

#include "kernel/driver/driver_manager.h"

#define DRIVER_IMAGE_VA_BASE  0x0000000070000000ULL
#define DRIVER_IMAGE_VA_LIMIT 0x0000000078000000ULL
#define DRIVER_IMAGE_VA_GUARD_PAGES 1u
#define DRIVER_IMAGE_VA_MAX_ALLOCATIONS DRIVER_MAX_RESOURCES
#define DRIVER_IMAGE_VA_MAX_EXTENTS (DRIVER_IMAGE_VA_MAX_ALLOCATIONS + 1u)

struct DriverVaHandle {
    uint32_t slot;
    uint32_t generation;
};

struct DriverVaStats {
    uint64_t arena_pages;
    uint64_t free_pages;
    uint64_t largest_free_pages;
    uint64_t allocations;
    uint64_t releases;
    uint64_t reused_allocations;
    uint64_t guard_pages;
    uint32_t active;
    uint32_t quarantined;
    uint32_t free_extents;
    uint32_t high_water;
    uint64_t exhaustion_failures;
    uint64_t stale_rejections;
    uint64_t owner_rejections;
};

void driver_image_va_init();
DriverVaHandle driver_image_va_invalid();
int driver_image_va_handle_is_valid(DriverVaHandle handle);
int driver_image_va_allocate(DriverIdentity owner,
                             uint32_t page_count,
                             DriverVaHandle* out_handle,
                             uint64_t* out_base);
int driver_image_va_release(DriverIdentity owner, DriverVaHandle handle);
int driver_image_va_quarantine(DriverIdentity owner, DriverVaHandle handle);
void driver_image_va_get_stats(DriverVaStats* out);

#ifdef OS64_HOST_TEST
int driver_image_va_reset_for_test(uint64_t base, uint64_t limit,
                                   uint32_t guard_pages);
#endif

#endif
