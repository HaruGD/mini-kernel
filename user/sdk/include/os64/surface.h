#ifndef OS64_SDK_SURFACE_H
#define OS64_SDK_SURFACE_H

#include <stdint.h>

#include "os64/graphics_types.h"
#include "os64/handle_types.h"
#include "os64/surface_types.h"

#define OS_SURFACE_TEXT_TRANSPARENT_BG 0x00000001u

typedef struct OsSurfaceCanvas {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsSurfaceCanvas;

OsHandle os_surface_create(uint32_t width, uint32_t height, uint32_t pixel_format);
long os_surface_get_info(OsHandle surface, OsGraphicsSurfaceHandleInfo* info);
void* os_surface_map(OsHandle surface, uint32_t map_flags);
long os_surface_unmap(OsHandle surface, void* address);
long os_surface_close(OsHandle surface);
long os_surface_canvas_init(OsSurfaceCanvas* canvas,
                            uint32_t* pixels,
                            const OsGraphicsSurfaceHandleInfo* info);
long os_surface_canvas_put_pixel(OsSurfaceCanvas* canvas,
                                 int32_t x,
                                 int32_t y,
                                 uint32_t color);
long os_surface_canvas_fill_rect(OsSurfaceCanvas* canvas,
                                 OsRect rect,
                                 uint32_t color);
long os_surface_canvas_draw_line(OsSurfaceCanvas* canvas,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 uint32_t color);
long os_surface_canvas_draw_text(OsSurfaceCanvas* canvas,
                                 int32_t x,
                                 int32_t y,
                                 const char* text,
                                 uint32_t foreground,
                                 uint32_t background,
                                 uint32_t flags);

#endif
