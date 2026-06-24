#include <os64/os64.h>
#include "internal.h"

typedef struct OsGraphicsRectCommand {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
} OsGraphicsRectCommand;

_Static_assert(sizeof(OsGraphicsRectCommand) == 20, "graphics rectangle ABI changed");

#define SDK_FONT_WIDTH 5
#define SDK_FONT_HEIGHT 7
#define SDK_FONT_ADVANCE 6
#define SDK_FONT_LINE_HEIGHT 8

static const uint8_t SDK_FONT_DIGITS[10][SDK_FONT_HEIGHT] = {
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

static const uint8_t SDK_FONT_LETTERS[26][SDK_FONT_HEIGHT] = {
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

static const uint8_t SDK_FONT_QUESTION[SDK_FONT_HEIGHT] = {
    0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04
};

static int sdk_abs_i32(int32_t value) {
    if (value < 0) {
        return value == INT32_MIN ? INT32_MAX : -value;
    }
    return value;
}

static uint32_t bitmap_to_logical_color(uint32_t pixel_format, uint32_t native_color) {
    if (pixel_format == OS64_PIXEL_FORMAT_BGR) {
        uint32_t blue = (native_color >> 16) & 0xFFu;
        uint32_t green = (native_color >> 8) & 0xFFu;
        uint32_t red = native_color & 0xFFu;
        return OS_RGB(red, green, blue);
    }
    return native_color & 0x00FFFFFFu;
}

static int bitmap_is_valid(const OsBitmap* bitmap) {
    return bitmap != 0 &&
           bitmap->pixels != 0 &&
           bitmap->width != 0 &&
           bitmap->height != 0 &&
           bitmap->stride_pixels >= bitmap->width &&
           (bitmap->pixel_format == OS64_PIXEL_FORMAT_RGB ||
            bitmap->pixel_format == OS64_PIXEL_FORMAT_BGR);
}

static uint8_t sdk_glyph_row(char ch, uint32_t row) {
    if (row >= SDK_FONT_HEIGHT) {
        return 0;
    }
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    if (ch >= 'A' && ch <= 'Z') {
        return SDK_FONT_LETTERS[ch - 'A'][row];
    }
    if (ch >= '0' && ch <= '9') {
        return SDK_FONT_DIGITS[ch - '0'][row];
    }
    switch (ch) {
        case ' ': return 0x00;
        case '.': return row == 6 ? 0x04 : 0x00;
        case ':': return (row == 2 || row == 5) ? 0x04 : 0x00;
        case '-': return row == 3 ? 0x1F : 0x00;
        case '_': return row == 6 ? 0x1F : 0x00;
        case '/': return (row >= 1 && row <= 5) ? (0x01 << (row - 1)) : 0x00;
        case '?': return SDK_FONT_QUESTION[row];
        default: return SDK_FONT_QUESTION[row];
    }
}

long os_gfx_get_info(OsGraphicsInfo* info) {
    if (info == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall1(OS_SYS_GFX_GET_INFO, (long)info);
}

long os_gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    return os_syscall3(OS_SYS_GFX_PUT_PIXEL, x, y, color);
}

long os_gfx_fill_rect(uint32_t x,
                      uint32_t y,
                      uint32_t width,
                      uint32_t height,
                      uint32_t color) {
    OsGraphicsRectCommand command;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.color = color;
    return os_syscall1(OS_SYS_GFX_FILL_RECT, (long)&command);
}

long os_gfx_clear(uint32_t color) {
    return os_syscall1(OS_SYS_GFX_CLEAR, color);
}

long os_gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (y0 == y1) {
        if (y0 < 0) {
            return OS_SUCCESS;
        }
        int32_t x = x0 < x1 ? x0 : x1;
        int32_t width = sdk_abs_i32(x1 - x0) + 1;
        if (x < 0) {
            width += x;
            x = 0;
        }
        if (width <= 0) {
            return OS_SUCCESS;
        }
        return os_gfx_fill_rect((uint32_t)x, (uint32_t)y0, (uint32_t)width, 1, color);
    }
    if (x0 == x1) {
        if (x0 < 0) {
            return OS_SUCCESS;
        }
        int32_t y = y0 < y1 ? y0 : y1;
        int32_t height = sdk_abs_i32(y1 - y0) + 1;
        if (y < 0) {
            height += y;
            y = 0;
        }
        if (height <= 0) {
            return OS_SUCCESS;
        }
        return os_gfx_fill_rect((uint32_t)x0, (uint32_t)y, 1, (uint32_t)height, color);
    }

    int32_t dx = sdk_abs_i32(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -sdk_abs_i32(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    long result = OS_SUCCESS;

    while (1) {
        if (x0 >= 0 && y0 >= 0) {
            long pixel_result = os_gfx_put_pixel((uint32_t)x0, (uint32_t)y0, color);
            if (pixel_result != OS_SUCCESS && pixel_result != OS_ERR_OUT_OF_RANGE) {
                result = pixel_result;
            }
        }
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
    return result;
}

long os_gfx_blit(const OsBitmap* bitmap, const OsRect* source_rect, int32_t destination_x, int32_t destination_y) {
    if (!bitmap_is_valid(bitmap) || source_rect == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    long result = OS_SUCCESS;
    for (int32_t y = 0; y < source_rect->height; y++) {
        int32_t source_y = source_rect->y + y;
        int32_t target_y = destination_y + y;
        if (source_y < 0 || target_y < 0 || (uint32_t)source_y >= bitmap->height) {
            continue;
        }
        for (int32_t x = 0; x < source_rect->width; x++) {
            int32_t source_x = source_rect->x + x;
            int32_t target_x = destination_x + x;
            if (source_x < 0 || target_x < 0 || (uint32_t)source_x >= bitmap->width) {
                continue;
            }
            uint32_t native = bitmap->pixels[(uint32_t)source_y * bitmap->stride_pixels + (uint32_t)source_x];
            uint32_t color = bitmap_to_logical_color(bitmap->pixel_format, native);
            long pixel_result = os_gfx_put_pixel((uint32_t)target_x, (uint32_t)target_y, color);
            if (pixel_result != OS_SUCCESS && pixel_result != OS_ERR_OUT_OF_RANGE) {
                result = pixel_result;
            }
        }
    }
    return result;
}

long os_gfx_blit_keyed(const OsBitmap* bitmap,
                       const OsRect* source_rect,
                       int32_t destination_x,
                       int32_t destination_y,
                       uint32_t color_key) {
    if (!bitmap_is_valid(bitmap) || source_rect == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    long result = OS_SUCCESS;
    uint32_t normalized_key = color_key & 0x00FFFFFFu;
    for (int32_t y = 0; y < source_rect->height; y++) {
        int32_t source_y = source_rect->y + y;
        int32_t target_y = destination_y + y;
        if (source_y < 0 || target_y < 0 || (uint32_t)source_y >= bitmap->height) {
            continue;
        }
        for (int32_t x = 0; x < source_rect->width; x++) {
            int32_t source_x = source_rect->x + x;
            int32_t target_x = destination_x + x;
            if (source_x < 0 || target_x < 0 || (uint32_t)source_x >= bitmap->width) {
                continue;
            }
            uint32_t native = bitmap->pixels[(uint32_t)source_y * bitmap->stride_pixels + (uint32_t)source_x];
            uint32_t color = bitmap_to_logical_color(bitmap->pixel_format, native);
            if ((color & 0x00FFFFFFu) == normalized_key) {
                continue;
            }
            long pixel_result = os_gfx_put_pixel((uint32_t)target_x, (uint32_t)target_y, color);
            if (pixel_result != OS_SUCCESS && pixel_result != OS_ERR_OUT_OF_RANGE) {
                result = pixel_result;
            }
        }
    }
    return result;
}

long os_gfx_draw_text(int32_t x,
                      int32_t y,
                      const char* text,
                      uint32_t foreground,
                      uint32_t background,
                      uint32_t flags) {
    if (text == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    long result = OS_SUCCESS;
    int32_t cursor_x = x;
    int32_t cursor_y = y;
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += SDK_FONT_LINE_HEIGHT;
            continue;
        }

        for (uint32_t row = 0; row < SDK_FONT_HEIGHT; row++) {
            uint8_t bits = sdk_glyph_row(ch, row);
            for (uint32_t column = 0; column < SDK_FONT_WIDTH; column++) {
                int pixel_on = (bits & (1u << (SDK_FONT_WIDTH - 1u - column))) != 0;
                uint32_t color = pixel_on ? foreground : background;
                if (!pixel_on && (flags & OS_GFX_TEXT_TRANSPARENT_BG)) {
                    continue;
                }
                int32_t px = cursor_x + (int32_t)column;
                int32_t py = cursor_y + (int32_t)row;
                if (px < 0 || py < 0) {
                    continue;
                }
                long pixel_result = os_gfx_put_pixel((uint32_t)px, (uint32_t)py, color);
                if (pixel_result != OS_SUCCESS && pixel_result != OS_ERR_OUT_OF_RANGE) {
                    result = pixel_result;
                }
            }
        }
        cursor_x += SDK_FONT_ADVANCE;
    }
    return result;
}
