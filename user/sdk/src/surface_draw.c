#include "os64/surface.h"

#include <limits.h>

#include "os64/result.h"

#define FONT_WIDTH 5u
#define FONT_HEIGHT 7u
#define FONT_ADVANCE 6
#define FONT_LINE_HEIGHT 8

static const uint8_t FONT_DIGITS[10][FONT_HEIGHT] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
};

static const uint8_t FONT_LETTERS[26][FONT_HEIGHT] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
};

static int canvas_valid(const OsSurfaceCanvas* canvas) {
    return canvas != 0 && canvas->pixels != 0 && canvas->width != 0 &&
           canvas->height != 0 && canvas->width <= INT32_MAX &&
           canvas->height <= INT32_MAX &&
           canvas->stride_pixels >= canvas->width &&
           (canvas->pixel_format == OS64_PIXEL_FORMAT_RGB ||
            canvas->pixel_format == OS64_PIXEL_FORMAT_BGR);
}

static uint32_t native_color(const OsSurfaceCanvas* canvas, uint32_t color) {
    (void)canvas;
    return color & 0x00FFFFFFu;
}

static uint8_t glyph_row(char character, uint32_t row) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    if (character >= 'A' && character <= 'Z') {
        return FONT_LETTERS[character - 'A'][row];
    }
    if (character >= '0' && character <= '9') {
        return FONT_DIGITS[character - '0'][row];
    }
    switch (character) {
        case ' ': return 0;
        case '.': return row == 6 ? 0x04 : 0;
        case ':': return row == 2 || row == 5 ? 0x04 : 0;
        case '-': return row == 3 ? 0x1F : 0;
        case '_': return row == 6 ? 0x1F : 0;
        case '/': return row >= 1 && row <= 5 ? (uint8_t)(1u << (row - 1u)) : 0;
        default: return (uint8_t[]){0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04}[row];
    }
}

long os_surface_canvas_init(OsSurfaceCanvas* canvas,
                            uint32_t* pixels,
                            const OsGraphicsSurfaceHandleInfo* info) {
    if (canvas == 0 || pixels == 0 || info == 0 || info->width == 0 ||
        info->height == 0 || info->width > INT32_MAX ||
        info->height > INT32_MAX || info->stride_pixels < info->width ||
        (info->pixel_format != OS64_PIXEL_FORMAT_RGB &&
         info->pixel_format != OS64_PIXEL_FORMAT_BGR)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    canvas->pixels = pixels;
    canvas->width = info->width;
    canvas->height = info->height;
    canvas->stride_pixels = info->stride_pixels;
    canvas->pixel_format = info->pixel_format;
    return OS_SUCCESS;
}

long os_surface_canvas_put_pixel(OsSurfaceCanvas* canvas,
                                 int32_t x,
                                 int32_t y,
                                 uint32_t color) {
    if (!canvas_valid(canvas)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    if (x < 0 || y < 0 || (uint32_t)x >= canvas->width ||
        (uint32_t)y >= canvas->height) {
        return OS_ERR_OUT_OF_RANGE;
    }
    canvas->pixels[(uint32_t)y * canvas->stride_pixels + (uint32_t)x] =
        native_color(canvas, color);
    return OS_SUCCESS;
}

long os_surface_canvas_fill_rect(OsSurfaceCanvas* canvas,
                                 OsRect rect,
                                 uint32_t color) {
    if (!canvas_valid(canvas) || rect.width <= 0 || rect.height <= 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    int64_t left64 = rect.x < 0 ? 0 : rect.x;
    int64_t top64 = rect.y < 0 ? 0 : rect.y;
    int64_t right64 = (int64_t)rect.x + rect.width;
    int64_t bottom64 = (int64_t)rect.y + rect.height;
    if (right64 > canvas->width) right64 = canvas->width;
    if (bottom64 > canvas->height) bottom64 = canvas->height;
    if (right64 <= left64 || bottom64 <= top64) {
        return OS_SUCCESS;
    }
    int32_t left = (int32_t)left64;
    int32_t top = (int32_t)top64;
    int32_t right = (int32_t)right64;
    int32_t bottom = (int32_t)bottom64;
    uint32_t native = native_color(canvas, color);
    for (int32_t y = top; y < bottom; y++) {
        for (int32_t x = left; x < right; x++) {
            canvas->pixels[(uint32_t)y * canvas->stride_pixels + (uint32_t)x] =
                native;
        }
    }
    return OS_SUCCESS;
}

#define LINE_LEFT   0x01u
#define LINE_RIGHT  0x02u
#define LINE_TOP    0x04u
#define LINE_BOTTOM 0x08u

static uint32_t line_out_code(const OsSurfaceCanvas* canvas,
                              int64_t x,
                              int64_t y) {
    uint32_t code = 0;
    if (x < 0) code |= LINE_LEFT;
    else if (x >= canvas->width) code |= LINE_RIGHT;
    if (y < 0) code |= LINE_TOP;
    else if (y >= canvas->height) code |= LINE_BOTTOM;
    return code;
}

static int clip_line(const OsSurfaceCanvas* canvas,
                     int64_t* x0,
                     int64_t* y0,
                     int64_t* x1,
                     int64_t* y1) {
    int64_t right = (int64_t)canvas->width - 1;
    int64_t bottom = (int64_t)canvas->height - 1;
    for (uint32_t iteration = 0; iteration < 16; iteration++) {
        uint32_t code0 = line_out_code(canvas, *x0, *y0);
        uint32_t code1 = line_out_code(canvas, *x1, *y1);
        if ((code0 | code1) == 0) return 1;
        if ((code0 & code1) != 0) return 0;
        uint32_t outside = code0 != 0 ? code0 : code1;
        int64_t x;
        int64_t y;
        if ((outside & LINE_TOP) != 0) {
            x = *x0 + (*x1 - *x0) * (-*y0) / (*y1 - *y0);
            y = 0;
        } else if ((outside & LINE_BOTTOM) != 0) {
            x = *x0 + (*x1 - *x0) * (bottom - *y0) / (*y1 - *y0);
            y = bottom;
        } else if ((outside & LINE_RIGHT) != 0) {
            y = *y0 + (*y1 - *y0) * (right - *x0) / (*x1 - *x0);
            x = right;
        } else {
            y = *y0 + (*y1 - *y0) * (-*x0) / (*x1 - *x0);
            x = 0;
        }
        if (outside == code0) {
            *x0 = x;
            *y0 = y;
        } else {
            *x1 = x;
            *y1 = y;
        }
    }
    return 0;
}

long os_surface_canvas_draw_line(OsSurfaceCanvas* canvas,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 uint32_t color) {
    if (!canvas_valid(canvas)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    int64_t current_x = x0;
    int64_t current_y = y0;
    int64_t end_x = x1;
    int64_t end_y = y1;
    if (!clip_line(canvas, &current_x, &current_y, &end_x, &end_y)) {
        return OS_SUCCESS;
    }
    int64_t dx = end_x >= current_x ? end_x - current_x
                                      : current_x - end_x;
    int64_t sx = current_x < end_x ? 1 : -1;
    int64_t dy_distance = end_y >= current_y ? end_y - current_y
                                               : current_y - end_y;
    int64_t dy = -dy_distance;
    int64_t sy = current_y < end_y ? 1 : -1;
    int64_t error = dx + dy;
    while (1) {
        if (current_x >= 0 && current_y >= 0 &&
            (uint64_t)current_x < canvas->width &&
            (uint64_t)current_y < canvas->height) {
            os_surface_canvas_put_pixel(canvas, (int32_t)current_x,
                                        (int32_t)current_y, color);
        }
        if (current_x == end_x && current_y == end_y) break;
        int64_t twice = error * 2;
        if (twice >= dy) { error += dy; current_x += sx; }
        if (twice <= dx) { error += dx; current_y += sy; }
    }
    return OS_SUCCESS;
}

long os_surface_canvas_draw_text(OsSurfaceCanvas* canvas,
                                 int32_t x,
                                 int32_t y,
                                 const char* text,
                                 uint32_t foreground,
                                 uint32_t background,
                                 uint32_t flags) {
    if (!canvas_valid(canvas) || text == 0 ||
        (flags & ~OS_SURFACE_TEXT_TRANSPARENT_BG) != 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    int64_t cursor_x = x;
    int64_t cursor_y = y;
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            cursor_x = x;
            cursor_y += FONT_LINE_HEIGHT;
            continue;
        }
        for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
            uint8_t bits = glyph_row(text[i], row);
            for (uint32_t column = 0; column < FONT_WIDTH; column++) {
                int on = (bits & (1u << (FONT_WIDTH - 1u - column))) != 0;
                if (!on && (flags & OS_SURFACE_TEXT_TRANSPARENT_BG) != 0) {
                    continue;
                }
                int64_t px = cursor_x + column;
                int64_t py = cursor_y + row;
                if (px >= 0 && py >= 0 && (uint64_t)px < canvas->width &&
                    (uint64_t)py < canvas->height) {
                    os_surface_canvas_put_pixel(canvas, (int32_t)px,
                                                (int32_t)py,
                                                on ? foreground : background);
                }
            }
        }
        cursor_x += FONT_ADVANCE;
    }
    return OS_SUCCESS;
}
