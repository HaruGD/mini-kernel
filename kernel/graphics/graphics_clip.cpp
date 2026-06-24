#include "kernel/graphics/graphics2d.h"

static void clear_rect(OsRect* rect) {
    if (rect == 0) {
        return;
    }
    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
}

int gfx_rect_is_empty(const OsRect* rect) {
    return rect == 0 || rect->width <= 0 || rect->height <= 0;
}

int gfx_clip_rect(const OsRect* bounds, const OsRect* input, OsRect* output) {
    if (gfx_rect_is_empty(bounds) || gfx_rect_is_empty(input) || output == 0) {
        clear_rect(output);
        return 0;
    }

    int64_t bounds_left = bounds->x;
    int64_t bounds_top = bounds->y;
    int64_t bounds_right = bounds_left + bounds->width;
    int64_t bounds_bottom = bounds_top + bounds->height;
    int64_t input_left = input->x;
    int64_t input_top = input->y;
    int64_t input_right = input_left + input->width;
    int64_t input_bottom = input_top + input->height;

    int64_t clipped_left = input_left < bounds_left ? bounds_left : input_left;
    int64_t clipped_top = input_top < bounds_top ? bounds_top : input_top;
    int64_t clipped_right = input_right > bounds_right ? bounds_right : input_right;
    int64_t clipped_bottom = input_bottom > bounds_bottom ? bounds_bottom : input_bottom;

    if (clipped_right <= clipped_left || clipped_bottom <= clipped_top ||
        clipped_left < INT32_MIN || clipped_top < INT32_MIN ||
        clipped_right > INT32_MAX || clipped_bottom > INT32_MAX) {
        clear_rect(output);
        return 0;
    }

    output->x = (int32_t)clipped_left;
    output->y = (int32_t)clipped_top;
    output->width = (int32_t)(clipped_right - clipped_left);
    output->height = (int32_t)(clipped_bottom - clipped_top);
    return 1;
}
