#include "kernel/graphics/graphics2d.h"

uint32_t gfx_present_dirty_surface(GraphicsSurface* destination,
                                   const GraphicsSurface* source,
                                   GraphicsDirtyTracker* dirty_tracker) {
    if (!gfx_surface_is_valid(destination) ||
        !gfx_surface_is_valid(source) ||
        dirty_tracker == 0) {
        return 0;
    }

    uint32_t dirty_count = gfx_dirty_count(dirty_tracker);
    const OsRect* rects = gfx_dirty_rects(dirty_tracker);
    for (uint32_t i = 0; i < dirty_count; i++) {
        gfx_blit(destination, source, &rects[i], rects[i].x, rects[i].y);
    }
    gfx_dirty_clear(dirty_tracker);
    return dirty_count;
}
