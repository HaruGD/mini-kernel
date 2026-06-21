#ifndef OS64_GRAPHICS_H
#define OS64_GRAPHICS_H

#include <stdint.h>

#define OS_GFX_PIXEL_RGB 0u
#define OS_GFX_PIXEL_BGR 1u
#define OS_RGB(red, green, blue) \
    ((((uint32_t)(red) & 0xFFu) << 16) | (((uint32_t)(green) & 0xFFu) << 8) | \
     ((uint32_t)(blue) & 0xFFu))

typedef struct OsGraphicsInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t format;
} OsGraphicsInfo;

long os_gfx_get_info(OsGraphicsInfo* info);
long os_gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color);
long os_gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
long os_gfx_clear(uint32_t color);

#endif
