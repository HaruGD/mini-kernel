#include "kernel/graphics/graphics2d.h"

uint32_t gfx_surface_to_native_color(const GraphicsSurface* surface, uint32_t color) {
    uint32_t red = (color >> 16) & 0xFFu;
    uint32_t green = (color >> 8) & 0xFFu;
    uint32_t blue = color & 0xFFu;

    if (surface != 0 && surface->pixel_format == OS64_PIXEL_FORMAT_BGR) {
        return (blue << 16) | (green << 8) | red;
    }
    return (red << 16) | (green << 8) | blue;
}

void gfx_put_pixel(GraphicsSurface* surface, int32_t x, int32_t y, uint32_t color) {
    if (!gfx_surface_contains_point(surface, x, y)) {
        return;
    }

    uint32_t native_color = gfx_surface_to_native_color(surface, color);
    surface->pixels[(uint64_t)(uint32_t)y * surface->stride_pixels + (uint32_t)x] = native_color;
}

void gfx_fill_rect(GraphicsSurface* surface, const OsRect* rect, uint32_t color) {
    OsRect bounds;
    OsRect clipped;
    if (!gfx_surface_bounds(surface, &bounds) ||
        !gfx_clip_rect(&bounds, rect, &clipped)) {
        return;
    }

    uint32_t native_color = gfx_surface_to_native_color(surface, color);
    uint32_t x_end = (uint32_t)(clipped.x + clipped.width);
    uint32_t y_end = (uint32_t)(clipped.y + clipped.height);

    for (uint32_t y = (uint32_t)clipped.y; y < y_end; y++) {
        uint32_t* row = surface->pixels + (uint64_t)y * surface->stride_pixels;
        for (uint32_t x = (uint32_t)clipped.x; x < x_end; x++) {
            row[x] = native_color;
        }
    }
}
