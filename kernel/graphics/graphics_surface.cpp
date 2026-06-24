#include "kernel/graphics/graphics2d.h"

static int valid_pixel_format(uint32_t pixel_format) {
    return pixel_format == OS64_PIXEL_FORMAT_RGB ||
           pixel_format == OS64_PIXEL_FORMAT_BGR;
}

static void clear_surface(GraphicsSurface* surface) {
    if (surface == 0) {
        return;
    }
    surface->pixels = 0;
    surface->width = 0;
    surface->height = 0;
    surface->stride_pixels = 0;
    surface->pixel_format = OS64_PIXEL_FORMAT_RGB;
    surface->flags = 0;
}

int gfx_surface_init(GraphicsSurface* surface,
                     uint32_t* pixels,
                     uint32_t width,
                     uint32_t height,
                     uint32_t stride_pixels,
                     uint32_t pixel_format,
                     uint32_t flags) {
    if (surface == 0 || pixels == 0 ||
        width == 0 || height == 0 ||
        stride_pixels < width ||
        width > (uint32_t)INT32_MAX ||
        height > (uint32_t)INT32_MAX ||
        !valid_pixel_format(pixel_format)) {
        clear_surface(surface);
        return 0;
    }

    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride_pixels = stride_pixels;
    surface->pixel_format = pixel_format;
    surface->flags = flags;
    return 1;
}

int gfx_surface_is_valid(const GraphicsSurface* surface) {
    return surface != 0 &&
           surface->pixels != 0 &&
           surface->width != 0 &&
           surface->height != 0 &&
           surface->stride_pixels >= surface->width &&
           surface->width <= (uint32_t)INT32_MAX &&
           surface->height <= (uint32_t)INT32_MAX &&
           valid_pixel_format(surface->pixel_format);
}

int gfx_surface_get_info(const GraphicsSurface* surface, OsSurfaceInfo* info) {
    if (!gfx_surface_is_valid(surface) || info == 0) {
        if (info != 0) {
            info->width = 0;
            info->height = 0;
            info->stride_pixels = 0;
            info->pixel_format = OS64_PIXEL_FORMAT_RGB;
        }
        return 0;
    }

    info->width = surface->width;
    info->height = surface->height;
    info->stride_pixels = surface->stride_pixels;
    info->pixel_format = surface->pixel_format;
    return 1;
}

int gfx_surface_contains_point(const GraphicsSurface* surface, int32_t x, int32_t y) {
    if (!gfx_surface_is_valid(surface) || x < 0 || y < 0) {
        return 0;
    }
    return (uint32_t)x < surface->width && (uint32_t)y < surface->height;
}

int gfx_surface_bounds(const GraphicsSurface* surface, OsRect* bounds) {
    if (!gfx_surface_is_valid(surface) || bounds == 0) {
        if (bounds != 0) {
            bounds->x = 0;
            bounds->y = 0;
            bounds->width = 0;
            bounds->height = 0;
        }
        return 0;
    }

    bounds->x = 0;
    bounds->y = 0;
    bounds->width = (int32_t)surface->width;
    bounds->height = (int32_t)surface->height;
    return 1;
}
