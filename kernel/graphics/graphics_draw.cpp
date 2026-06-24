#include "kernel/graphics/graphics2d.h"
#include "kernel/graphics/graphics_font.h"

uint32_t gfx_surface_to_native_color(const GraphicsSurface* surface, uint32_t color) {
    uint32_t red = (color >> 16) & 0xFFu;
    uint32_t green = (color >> 8) & 0xFFu;
    uint32_t blue = color & 0xFFu;

    if (surface != 0 && surface->pixel_format == OS64_PIXEL_FORMAT_BGR) {
        return (blue << 16) | (green << 8) | red;
    }
    return (red << 16) | (green << 8) | blue;
}

uint32_t gfx_surface_from_native_color(const GraphicsSurface* surface, uint32_t native_color) {
    if (surface != 0 && surface->pixel_format == OS64_PIXEL_FORMAT_BGR) {
        uint32_t blue = (native_color >> 16) & 0xFFu;
        uint32_t green = (native_color >> 8) & 0xFFu;
        uint32_t red = native_color & 0xFFu;
        return (red << 16) | (green << 8) | blue;
    }
    return native_color & 0x00FFFFFFu;
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

void gfx_draw_hline(GraphicsSurface* surface,
                    int32_t x,
                    int32_t y,
                    int32_t width,
                    uint32_t color) {
    OsRect rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = 1;
    gfx_fill_rect(surface, &rect, color);
}

void gfx_draw_vline(GraphicsSurface* surface,
                    int32_t x,
                    int32_t y,
                    int32_t height,
                    uint32_t color) {
    OsRect rect;
    rect.x = x;
    rect.y = y;
    rect.width = 1;
    rect.height = height;
    gfx_fill_rect(surface, &rect, color);
}

static int abs_i32(int32_t value) {
    if (value < 0) {
        return value == INT32_MIN ? INT32_MAX : -value;
    }
    return value;
}

static int line_may_touch_surface(const GraphicsSurface* surface,
                                  int32_t x0,
                                  int32_t y0,
                                  int32_t x1,
                                  int32_t y1) {
    if (!gfx_surface_is_valid(surface)) {
        return 0;
    }

    int32_t min_x = x0 < x1 ? x0 : x1;
    int32_t max_x = x0 > x1 ? x0 : x1;
    int32_t min_y = y0 < y1 ? y0 : y1;
    int32_t max_y = y0 > y1 ? y0 : y1;
    if (max_x < 0 || max_y < 0) {
        return 0;
    }
    if (min_x >= (int32_t)surface->width || min_y >= (int32_t)surface->height) {
        return 0;
    }
    return 1;
}

void gfx_draw_line(GraphicsSurface* surface,
                   int32_t x0,
                   int32_t y0,
                   int32_t x1,
                   int32_t y1,
                   uint32_t color) {
    if (!line_may_touch_surface(surface, x0, y0, x1, y1)) {
        return;
    }

    if (y0 == y1) {
        int32_t x = x0 < x1 ? x0 : x1;
        int32_t width = abs_i32(x1 - x0) + 1;
        gfx_draw_hline(surface, x, y0, width, color);
        return;
    }
    if (x0 == x1) {
        int32_t y = y0 < y1 ? y0 : y1;
        int32_t height = abs_i32(y1 - y0) + 1;
        gfx_draw_vline(surface, x0, y, height, color);
        return;
    }

    int32_t dx = abs_i32(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs_i32(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;

    while (1) {
        gfx_put_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }

        int32_t twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

typedef struct BlitRegion {
    OsRect source;
    int32_t destination_x;
    int32_t destination_y;
} BlitRegion;

static int prepare_blit_region(GraphicsSurface* destination,
                               const GraphicsSurface* source,
                               const OsRect* source_rect,
                               int32_t destination_x,
                               int32_t destination_y,
                               BlitRegion* region) {
    OsRect source_bounds;
    if (!gfx_surface_bounds(source, &source_bounds) ||
        !gfx_surface_is_valid(destination) ||
        region == 0 ||
        !gfx_clip_rect(&source_bounds, source_rect, &region->source)) {
        return 0;
    }

    region->destination_x = destination_x + (region->source.x - source_rect->x);
    region->destination_y = destination_y + (region->source.y - source_rect->y);
    if (region->destination_x < 0) {
        int32_t delta = -region->destination_x;
        region->source.x += delta;
        region->source.width -= delta;
        region->destination_x = 0;
    }
    if (region->destination_y < 0) {
        int32_t delta = -region->destination_y;
        region->source.y += delta;
        region->source.height -= delta;
        region->destination_y = 0;
    }
    if (region->destination_x + region->source.width > (int32_t)destination->width) {
        region->source.width = (int32_t)destination->width - region->destination_x;
    }
    if (region->destination_y + region->source.height > (int32_t)destination->height) {
        region->source.height = (int32_t)destination->height - region->destination_y;
    }
    return region->source.width > 0 && region->source.height > 0;
}

void gfx_blit(GraphicsSurface* destination,
              const GraphicsSurface* source,
              const OsRect* source_rect,
              int32_t destination_x,
              int32_t destination_y) {
    BlitRegion region;
    if (!prepare_blit_region(destination, source, source_rect, destination_x, destination_y, &region)) {
        return;
    }

    for (int32_t row = 0; row < region.source.height; row++) {
        const uint32_t* source_row = source->pixels +
            (uint64_t)(uint32_t)(region.source.y + row) * source->stride_pixels;
        uint32_t* destination_row = destination->pixels +
            (uint64_t)(uint32_t)(region.destination_y + row) * destination->stride_pixels;
        for (int32_t column = 0; column < region.source.width; column++) {
            uint32_t source_native = source_row[(uint32_t)(region.source.x + column)];
            uint32_t color = gfx_surface_from_native_color(source, source_native);
            destination_row[(uint32_t)(region.destination_x + column)] =
                gfx_surface_to_native_color(destination, color);
        }
    }
}

void gfx_blit_keyed(GraphicsSurface* destination,
                    const GraphicsSurface* source,
                    const OsRect* source_rect,
                    int32_t destination_x,
                    int32_t destination_y,
                    uint32_t color_key) {
    BlitRegion region;
    if (!prepare_blit_region(destination, source, source_rect, destination_x, destination_y, &region)) {
        return;
    }

    uint32_t normalized_key = color_key & 0x00FFFFFFu;
    for (int32_t row = 0; row < region.source.height; row++) {
        const uint32_t* source_row = source->pixels +
            (uint64_t)(uint32_t)(region.source.y + row) * source->stride_pixels;
        uint32_t* destination_row = destination->pixels +
            (uint64_t)(uint32_t)(region.destination_y + row) * destination->stride_pixels;
        for (int32_t column = 0; column < region.source.width; column++) {
            uint32_t source_native = source_row[(uint32_t)(region.source.x + column)];
            uint32_t color = gfx_surface_from_native_color(source, source_native);
            if ((color & 0x00FFFFFFu) == normalized_key) {
                continue;
            }
            destination_row[(uint32_t)(region.destination_x + column)] =
                gfx_surface_to_native_color(destination, color);
        }
    }
}

void gfx_draw_glyph(GraphicsSurface* surface,
                    int32_t x,
                    int32_t y,
                    char ch,
                    uint32_t foreground,
                    uint32_t background,
                    uint32_t flags) {
    if (!gfx_surface_is_valid(surface)) {
        return;
    }

    for (uint32_t row = 0; row < GFX_FONT_HEIGHT; row++) {
        uint8_t bits = gfx_font_glyph_row(ch, row);
        for (uint32_t column = 0; column < GFX_FONT_WIDTH; column++) {
            int pixel_on = (bits & (1u << (GFX_FONT_WIDTH - 1u - column))) != 0;
            if (pixel_on) {
                gfx_put_pixel(surface, x + (int32_t)column, y + (int32_t)row, foreground);
            } else if ((flags & GFX_TEXT_FLAG_TRANSPARENT_BG) == 0) {
                gfx_put_pixel(surface, x + (int32_t)column, y + (int32_t)row, background);
            }
        }
    }
}

void gfx_draw_text(GraphicsSurface* surface,
                   int32_t x,
                   int32_t y,
                   const char* text,
                   uint32_t foreground,
                   uint32_t background,
                   uint32_t flags) {
    if (!gfx_surface_is_valid(surface) || text == 0) {
        return;
    }

    int32_t cursor_x = x;
    int32_t cursor_y = y;
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += GFX_FONT_LINE_HEIGHT;
            continue;
        }
        if (ch == '\r') {
            cursor_x = x;
            continue;
        }
        gfx_draw_glyph(surface, cursor_x, cursor_y, ch, foreground, background, flags);
        cursor_x += GFX_FONT_ADVANCE;
    }
}
