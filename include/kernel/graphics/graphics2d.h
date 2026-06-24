#ifndef KERNEL_GRAPHICS_2D_H
#define KERNEL_GRAPHICS_2D_H

#include "os64/graphics_types.h"

#define GFX_SURFACE_FLAG_FRAMEBUFFER 0x00000001u
#define GFX_SURFACE_FLAG_OWNS_PIXELS 0x00000002u

typedef struct GraphicsSurface {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t flags;
} GraphicsSurface;

#ifdef __cplusplus
extern "C" {
#endif

int gfx_rect_is_empty(const OsRect* rect);
int gfx_clip_rect(const OsRect* bounds, const OsRect* input, OsRect* output);
int gfx_surface_init(GraphicsSurface* surface,
                     uint32_t* pixels,
                     uint32_t width,
                     uint32_t height,
                     uint32_t stride_pixels,
                     uint32_t pixel_format,
                     uint32_t flags);
int gfx_surface_is_valid(const GraphicsSurface* surface);
int gfx_surface_get_info(const GraphicsSurface* surface, OsSurfaceInfo* info);
int gfx_surface_contains_point(const GraphicsSurface* surface, int32_t x, int32_t y);
int gfx_surface_bounds(const GraphicsSurface* surface, OsRect* bounds);
uint32_t gfx_surface_to_native_color(const GraphicsSurface* surface, uint32_t color);
void gfx_put_pixel(GraphicsSurface* surface, int32_t x, int32_t y, uint32_t color);
void gfx_fill_rect(GraphicsSurface* surface, const OsRect* rect, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif
