#ifndef KERNEL_GRAPHICS_SURFACE_BACKING_H
#define KERNEL_GRAPHICS_SURFACE_BACKING_H

#include <stdint.h>

#include "kernel/mm/vm.h"

#define KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS 16u
#define KERNEL_GRAPHICS_SURFACE_MAX_PIXELS (1920u * 1080u)
#define KERNEL_GRAPHICS_SURFACE_BYTES_PER_PIXEL 4u
#define KERNEL_GRAPHICS_SURFACE_MAX_PAGES \
    ((KERNEL_GRAPHICS_SURFACE_MAX_PIXELS * KERNEL_GRAPHICS_SURFACE_BYTES_PER_PIXEL + \
      (uint32_t)VM_PAGE_SIZE - 1u) / (uint32_t)VM_PAGE_SIZE)

#ifndef KERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES
#define KERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES 8192u
#endif

struct KernelGraphicsSurfaceBackingInfo {
    uint32_t active;
    uint32_t page_count;
    uint32_t byte_size;
    uint32_t reserved;
    uint64_t virtual_base;
};

struct KernelGraphicsSurfaceBackingStats {
    uint32_t active_backings;
    uint32_t mapped_pages;
    uint64_t mapped_bytes;
    uint64_t allocation_attempts;
    uint64_t allocation_failures;
    uint64_t rollback_pages;
    uint64_t release_pages;
    uint64_t unmap_failures;
};

void kernel_graphics_surface_backing_init();
int kernel_graphics_surface_backing_allocate(uint32_t slot,
                                             uint32_t byte_size,
                                             uint32_t** pixels_out,
                                             uint32_t* page_count_out);
void kernel_graphics_surface_backing_release(uint32_t slot);
int kernel_graphics_surface_backing_get_info(uint32_t slot,
                                             KernelGraphicsSurfaceBackingInfo* info);
uint64_t kernel_graphics_surface_backing_get_phys(uint32_t slot, uint32_t page);
void kernel_graphics_surface_backing_get_stats(KernelGraphicsSurfaceBackingStats* stats);

#endif
