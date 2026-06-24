#ifndef OS64_GRAPHICS_H
#define OS64_GRAPHICS_H

#include "os64/graphics_types.h"

long os_gfx_get_info(OsGraphicsInfo* info);
long os_gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color);
long os_gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
long os_gfx_clear(uint32_t color);

#endif
