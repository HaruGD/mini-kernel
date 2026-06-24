#ifndef KERNEL_GRAPHICS_FONT_H
#define KERNEL_GRAPHICS_FONT_H

#include <stdint.h>

#define GFX_FONT_WIDTH 5
#define GFX_FONT_HEIGHT 7
#define GFX_FONT_ADVANCE 6
#define GFX_FONT_LINE_HEIGHT 8

#ifdef __cplusplus
extern "C" {
#endif

uint8_t gfx_font_glyph_row(char ch, uint32_t row);
int gfx_font_has_direct_glyph(char ch);

#ifdef __cplusplus
}
#endif

#endif
