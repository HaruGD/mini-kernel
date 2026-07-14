#include "kernel/graphics/display_backend.h"

#include "drivers/gop.h"

struct DisplayBackendOps {
    int (*get_info)(DisplayBackendInfo* info);
    uint32_t (*present)(const GraphicsSurface* source,
                        const OsRect* rects,
                        uint32_t rect_count);
};

static DisplayBackendStats backend_stats;

static int gop_backend_get_info(DisplayBackendInfo* info) {
    const GOPInfo* gop_info = gop.info();
    if (info == 0 || gop_info == 0) {
        return 0;
    }
    info->width = gop_info->width;
    info->height = gop_info->height;
    info->stride_pixels = gop_info->pixels_per_scanline;
    info->pixel_format = gop_info->format;
    return 1;
}

static uint32_t gop_backend_present(const GraphicsSurface* source,
                                    const OsRect* rects,
                                    uint32_t rect_count) {
    return gop.present_surface(source, rects, rect_count);
}

static const DisplayBackendOps gop_backend_ops = {
    gop_backend_get_info,
    gop_backend_present,
};

static const DisplayBackendOps* active_backend = &gop_backend_ops;

static int tracker_covers_display(const GraphicsDirtyTracker* tracker,
                                  const OsRect* bounds) {
    const OsRect* rects = gfx_dirty_rects(tracker);
    return gfx_dirty_count(tracker) == 1 && rects != 0 && bounds != 0 &&
           rects[0].x == bounds->x && rects[0].y == bounds->y &&
           rects[0].width == bounds->width &&
           rects[0].height == bounds->height;
}

void display_backend_init() {
    backend_stats.present_count = 0;
    backend_stats.full_present_count = 0;
    backend_stats.presented_rects = 0;
    backend_stats.rejected_count = 0;
    active_backend = &gop_backend_ops;
}

int display_backend_get_info(DisplayBackendInfo* info) {
    return active_backend != 0 && active_backend->get_info(info)
        ? DISPLAY_BACKEND_OK
        : DISPLAY_BACKEND_ERR_NOT_READY;
}

int display_backend_present(const GraphicsSurface* source,
                            const OsRect* rects,
                            uint32_t rect_count,
                            uint32_t* presented_rects) {
    if (presented_rects != 0) {
        *presented_rects = 0;
    }
    DisplayBackendInfo info;
    if (source == 0 || rects == 0 || rect_count == 0 ||
        rect_count > DISPLAY_BACKEND_MAX_DAMAGE_RECTS ||
        display_backend_get_info(&info) != DISPLAY_BACKEND_OK ||
        !gfx_surface_is_valid(source) ||
        source->width != info.width || source->height != info.height ||
        source->stride_pixels < source->width ||
        (source->pixel_format != OS64_PIXEL_FORMAT_RGB &&
         source->pixel_format != OS64_PIXEL_FORMAT_BGR)) {
        backend_stats.rejected_count++;
        return DISPLAY_BACKEND_ERR_INVALID;
    }

    OsRect bounds;
    bounds.x = 0;
    bounds.y = 0;
    bounds.width = (int32_t)info.width;
    bounds.height = (int32_t)info.height;
    GraphicsDirtyTracker tracker;
    gfx_dirty_init(&tracker, &bounds);
    for (uint32_t i = 0; i < rect_count; i++) {
        gfx_dirty_mark(&tracker, &rects[i]);
    }
    uint32_t clipped_count = gfx_dirty_count(&tracker);
    if (clipped_count == 0) {
        backend_stats.rejected_count++;
        return DISPLAY_BACKEND_ERR_INVALID;
    }
    uint32_t count = active_backend->present(source,
                                             gfx_dirty_rects(&tracker),
                                             clipped_count);
    if (count == 0) {
        backend_stats.rejected_count++;
        return DISPLAY_BACKEND_ERR_IO;
    }
    backend_stats.present_count++;
    backend_stats.full_present_count +=
        (gfx_dirty_is_full(&tracker) || tracker_covers_display(&tracker, &bounds))
            ? 1u
            : 0u;
    backend_stats.presented_rects += count;
    if (presented_rects != 0) {
        *presented_rects = count;
    }
    return DISPLAY_BACKEND_OK;
}

void display_backend_get_stats(DisplayBackendStats* stats) {
    if (stats != 0) {
        *stats = backend_stats;
    }
}
