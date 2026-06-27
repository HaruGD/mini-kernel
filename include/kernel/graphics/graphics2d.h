#ifndef KERNEL_GRAPHICS_2D_H
#define KERNEL_GRAPHICS_2D_H

#include "os64/graphics_types.h"

#define GFX_SURFACE_FLAG_FRAMEBUFFER 0x00000001u
#define GFX_SURFACE_FLAG_OWNS_PIXELS 0x00000002u

#define GFX_TEXT_FLAG_TRANSPARENT_BG 0x00000001u

#define GFX_DIRTY_MAX_RECTS 64u

typedef struct GraphicsSurface {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t flags;
} GraphicsSurface;

typedef struct GraphicsDirtyTracker {
    OsRect bounds;
    OsRect rects[GFX_DIRTY_MAX_RECTS];
    uint32_t count;
    uint32_t full;
} GraphicsDirtyTracker;

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
uint32_t gfx_surface_from_native_color(const GraphicsSurface* surface, uint32_t native_color);
void gfx_put_pixel(GraphicsSurface* surface, int32_t x, int32_t y, uint32_t color);
void gfx_fill_rect(GraphicsSurface* surface, const OsRect* rect, uint32_t color);
void gfx_draw_hline(GraphicsSurface* surface,
                    int32_t x,
                    int32_t y,
                    int32_t width,
                    uint32_t color);
void gfx_draw_vline(GraphicsSurface* surface,
                    int32_t x,
                    int32_t y,
                    int32_t height,
                    uint32_t color);
void gfx_draw_line(GraphicsSurface* surface,
                   int32_t x0,
                   int32_t y0,
                   int32_t x1,
                   int32_t y1,
                   uint32_t color);
void gfx_blit(GraphicsSurface* destination,
              const GraphicsSurface* source,
              const OsRect* source_rect,
              int32_t destination_x,
              int32_t destination_y);
void gfx_blit_keyed(GraphicsSurface* destination,
                    const GraphicsSurface* source,
                    const OsRect* source_rect,
                    int32_t destination_x,
                    int32_t destination_y,
                    uint32_t color_key);
void gfx_draw_glyph(GraphicsSurface* surface,
                    int32_t x,
                    int32_t y,
                    char ch,
                    uint32_t foreground,
                    uint32_t background,
                    uint32_t flags);
void gfx_draw_text(GraphicsSurface* surface,
                   int32_t x,
                   int32_t y,
                   const char* text,
                   uint32_t foreground,
                   uint32_t background,
                   uint32_t flags);
void gfx_copy_surface(GraphicsSurface* destination, const GraphicsSurface* source);
uint32_t gfx_present_dirty_surface(GraphicsSurface* destination,
                                   const GraphicsSurface* source,
                                   GraphicsDirtyTracker* dirty_tracker);
void gfx_dirty_init(GraphicsDirtyTracker* tracker, const OsRect* bounds);
void gfx_dirty_clear(GraphicsDirtyTracker* tracker);
void gfx_dirty_mark(GraphicsDirtyTracker* tracker, const OsRect* rect);
void gfx_dirty_mark_full(GraphicsDirtyTracker* tracker);
uint32_t gfx_dirty_count(const GraphicsDirtyTracker* tracker);
uint32_t gfx_dirty_is_full(const GraphicsDirtyTracker* tracker);
const OsRect* gfx_dirty_rects(const GraphicsDirtyTracker* tracker);

#ifdef __cplusplus
}
#endif

#endif
