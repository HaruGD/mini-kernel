#ifndef KERNEL_GRAPHICS_DISPLAY_BACKEND_H
#define KERNEL_GRAPHICS_DISPLAY_BACKEND_H

#include <stdint.h>

#include "kernel/graphics/graphics2d.h"

#define DISPLAY_BACKEND_MAX_DAMAGE_RECTS 64u

#define DISPLAY_BACKEND_OK 0
#define DISPLAY_BACKEND_ERR_INVALID (-2)
#define DISPLAY_BACKEND_ERR_NOT_READY (-1)
#define DISPLAY_BACKEND_ERR_IO (-5)

struct DisplayBackendInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
};

struct DisplayBackendStats {
    uint64_t present_count;
    uint64_t full_present_count;
    uint64_t presented_rects;
    uint64_t rejected_count;
};

void display_backend_init();
int display_backend_get_info(DisplayBackendInfo* info);
int display_backend_present(const GraphicsSurface* source,
                            const OsRect* rects,
                            uint32_t rect_count,
                            uint32_t* presented_rects);
void display_backend_get_stats(DisplayBackendStats* stats);

#endif
