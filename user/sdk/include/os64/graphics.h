#ifndef OS64_GRAPHICS_H
#define OS64_GRAPHICS_H

#include "os64/graphics_types.h"

#define OS_GFX_TEXT_TRANSPARENT_BG 0x00000001u

typedef struct OsBitmap {
    const uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} OsBitmap;

long os_gfx_get_info(OsGraphicsInfo* info);
long os_gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color);
long os_gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
long os_gfx_clear(uint32_t color);
long os_gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
long os_gfx_blit(const OsBitmap* bitmap, const OsRect* source_rect, int32_t destination_x, int32_t destination_y);
long os_gfx_blit_keyed(const OsBitmap* bitmap,
                       const OsRect* source_rect,
                       int32_t destination_x,
                       int32_t destination_y,
                       uint32_t color_key);
long os_gfx_draw_text(int32_t x,
                      int32_t y,
                      const char* text,
                      uint32_t foreground,
                      uint32_t background,
                      uint32_t flags);

#endif
