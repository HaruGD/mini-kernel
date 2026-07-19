#include "window_compositor.h"

#include <limits.h>

#include <os64/result.h>

static int rect_bounds(OsRect rect,
                       int64_t* left,
                       int64_t* top,
                       int64_t* right,
                       int64_t* bottom) {
    if (rect.width <= 0 || rect.height <= 0) {
        return 0;
    }
    *left = rect.x;
    *top = rect.y;
    *right = (int64_t)rect.x + rect.width;
    *bottom = (int64_t)rect.y + rect.height;
    return *right > *left && *bottom > *top;
}

static int rect_clip(OsRect rect,
                     int64_t clip_left,
                     int64_t clip_top,
                     int64_t clip_right,
                     int64_t clip_bottom,
                     OsRect* result) {
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    if (result == 0 || !rect_bounds(rect, &left, &top, &right, &bottom)) {
        return 0;
    }
    if (left < clip_left) left = clip_left;
    if (top < clip_top) top = clip_top;
    if (right > clip_right) right = clip_right;
    if (bottom > clip_bottom) bottom = clip_bottom;
    if (right <= left || bottom <= top || left < INT32_MIN || top < INT32_MIN ||
        left > INT32_MAX || top > INT32_MAX || right - left > INT32_MAX ||
        bottom - top > INT32_MAX) {
        return 0;
    }
    result->x = (int32_t)left;
    result->y = (int32_t)top;
    result->width = (int32_t)(right - left);
    result->height = (int32_t)(bottom - top);
    return 1;
}

static int rects_touch(OsRect left_rect, OsRect right_rect) {
    int64_t left_a, top_a, right_a, bottom_a;
    int64_t left_b, top_b, right_b, bottom_b;
    return rect_bounds(left_rect, &left_a, &top_a, &right_a, &bottom_a) &&
           rect_bounds(right_rect, &left_b, &top_b, &right_b, &bottom_b) &&
           left_a <= right_b && left_b <= right_a &&
           top_a <= bottom_b && top_b <= bottom_a;
}

static OsRect rect_union(OsRect a, OsRect b) {
    int64_t al, at, ar, ab;
    int64_t bl, bt, br, bb;
    rect_bounds(a, &al, &at, &ar, &ab);
    rect_bounds(b, &bl, &bt, &br, &bb);
    int64_t left = al < bl ? al : bl;
    int64_t top = at < bt ? at : bt;
    int64_t right = ar > br ? ar : br;
    int64_t bottom = ab > bb ? ab : bb;
    OsRect result = {(int32_t)left, (int32_t)top,
                     (int32_t)(right - left), (int32_t)(bottom - top)};
    return result;
}

void window_damage_init(WindowDamageAccumulator* damage,
                        uint32_t screen_width,
                        uint32_t screen_height) {
    if (damage == 0) {
        return;
    }
    damage->count = 0;
    damage->full_screen = 0;
    damage->screen_width = screen_width;
    damage->screen_height = screen_height;
}

void window_damage_reset(WindowDamageAccumulator* damage) {
    if (damage != 0) {
        damage->count = 0;
        damage->full_screen = 0;
    }
}

void window_damage_full(WindowDamageAccumulator* damage) {
    if (damage == 0 || damage->screen_width == 0 || damage->screen_height == 0 ||
        damage->screen_width > INT32_MAX || damage->screen_height > INT32_MAX) {
        return;
    }
    damage->count = 1;
    damage->full_screen = 1;
    damage->rects[0].x = 0;
    damage->rects[0].y = 0;
    damage->rects[0].width = (int32_t)damage->screen_width;
    damage->rects[0].height = (int32_t)damage->screen_height;
}

long window_damage_add_screen(WindowDamageAccumulator* damage, OsRect rect) {
    if (damage == 0 || damage->screen_width == 0 || damage->screen_height == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    if (damage->full_screen) {
        return OS_SUCCESS;
    }
    OsRect clipped;
    if (!rect_clip(rect, 0, 0, damage->screen_width, damage->screen_height,
                   &clipped)) {
        return OS_SUCCESS;
    }
    for (uint32_t i = 0; i < damage->count; i++) {
        if (!rects_touch(damage->rects[i], clipped)) {
            continue;
        }
        damage->rects[i] = rect_union(damage->rects[i], clipped);
        for (uint32_t j = 0; j < damage->count; ) {
            if (j == i || !rects_touch(damage->rects[i], damage->rects[j])) {
                j++;
                continue;
            }
            damage->rects[i] = rect_union(damage->rects[i], damage->rects[j]);
            damage->rects[j] = damage->rects[damage->count - 1u];
            damage->count--;
            if (i == damage->count) {
                i = j;
            }
        }
        return OS_SUCCESS;
    }
    if (damage->count >= WINDOW_SCREEN_DAMAGE_MAX_RECTS) {
        window_damage_full(damage);
        return OS_SUCCESS;
    }
    damage->rects[damage->count++] = clipped;
    return OS_SUCCESS;
}

long window_damage_add_window(WindowDamageAccumulator* damage,
                              const WindowEntry* window,
                              OsRect surface_rect) {
    if (damage == 0 || window == 0 || !window->active ||
        window->width > INT32_MAX || window->height > INT32_MAX) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    OsRect local;
    if (!rect_clip(surface_rect, 0, 0, window->width, window->height, &local)) {
        return OS_SUCCESS;
    }
    int64_t screen_x = (int64_t)window->x + local.x;
    int64_t screen_y = (int64_t)window->y + local.y;
    if (screen_x < INT32_MIN || screen_x > INT32_MAX ||
        screen_y < INT32_MIN || screen_y > INT32_MAX) {
        return OS_SUCCESS;
    }
    OsRect screen = {(int32_t)screen_x, (int32_t)screen_y,
                     local.width, local.height};
    return window_damage_add_screen(damage, screen);
}

OsRect window_state_screen_rect(const WindowEntry* window) {
    OsRect rect = {0, 0, 0, 0};
    if (window != 0 && window->active && window->width <= INT32_MAX &&
        window->height <= INT32_MAX) {
        rect.x = window->x;
        rect.y = window->y;
        rect.width = (int32_t)window->width;
        rect.height = (int32_t)window->height;
    }
    return rect;
}

static void fill_rect(uint32_t* destination,
                      uint32_t stride,
                      OsRect rect,
                      uint32_t color) {
    for (int32_t y = 0; y < rect.height; y++) {
        uint32_t* row = destination + (uint32_t)(rect.y + y) * stride +
                        (uint32_t)rect.x;
        for (int32_t x = 0; x < rect.width; x++) {
            row[x] = color & 0x00FFFFFFu;
        }
    }
}

long window_compositor_compose(uint32_t* destination,
                               uint32_t destination_stride,
                               uint32_t screen_width,
                               uint32_t screen_height,
                               uint32_t background,
                               const WindowTable* table,
                               const WindowCompositorSource* sources,
                               const WindowDamageAccumulator* damage) {
    if (destination == 0 || table == 0 || sources == 0 || damage == 0 ||
        screen_width == 0 || screen_height == 0 ||
        destination_stride < screen_width) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t d = 0; d < damage->count; d++) {
        OsRect dirty;
        if (!rect_clip(damage->rects[d], 0, 0, screen_width, screen_height,
                       &dirty)) {
            continue;
        }
        fill_rect(destination, destination_stride, dirty, background);
        for (uint32_t z = 0; z < table->count; z++) {
            uint32_t slot = table->z_slots[z];
            if (slot >= OS_WINDOW_MAX_WINDOWS) {
                return OS_ERR_BAD_BUFFER;
            }
            const WindowEntry* window = &table->entries[slot];
            const WindowCompositorSource* source = &sources[slot];
            if (!window->active || !window->visible || source->pixels == 0 ||
                source->stride_pixels < window->width) {
                continue;
            }
            OsRect window_rect = window_state_screen_rect(window);
            int64_t left = dirty.x > window_rect.x ? dirty.x : window_rect.x;
            int64_t top = dirty.y > window_rect.y ? dirty.y : window_rect.y;
            int64_t dirty_right = (int64_t)dirty.x + dirty.width;
            int64_t dirty_bottom = (int64_t)dirty.y + dirty.height;
            int64_t window_right = (int64_t)window_rect.x + window_rect.width;
            int64_t window_bottom = (int64_t)window_rect.y + window_rect.height;
            int64_t right = dirty_right < window_right ? dirty_right : window_right;
            int64_t bottom = dirty_bottom < window_bottom ? dirty_bottom : window_bottom;
            if (right <= left || bottom <= top) {
                continue;
            }
            uint32_t source_x = (uint32_t)(left - window->x);
            uint32_t source_y = (uint32_t)(top - window->y);
            uint32_t copy_width = (uint32_t)(right - left);
            uint32_t copy_height = (uint32_t)(bottom - top);
            for (uint32_t y = 0; y < copy_height; y++) {
                uint32_t* destination_row = destination +
                    (uint32_t)(top + y) * destination_stride + (uint32_t)left;
                const uint32_t* source_row = source->pixels +
                    (source_y + y) * source->stride_pixels + source_x;
                for (uint32_t x = 0; x < copy_width; x++) {
                    destination_row[x] = source_row[x] & 0x00FFFFFFu;
                }
            }
        }
    }
    return OS_SUCCESS;
}

long window_compositor_compose_underlay(
    uint32_t* destination,
    uint32_t destination_stride,
    uint32_t screen_width,
    uint32_t screen_height,
    const WindowCompositorSource* underlay,
    const WindowTable* table,
    const WindowCompositorSource* sources,
    const WindowDamageAccumulator* damage) {
    if (destination == 0 || underlay == 0 || underlay->pixels == 0 ||
        underlay->stride_pixels < screen_width || table == 0 || sources == 0 ||
        damage == 0 || screen_width == 0 || screen_height == 0 ||
        destination_stride < screen_width) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t d = 0; d < damage->count; d++) {
        OsRect dirty;
        if (!rect_clip(damage->rects[d], 0, 0, screen_width, screen_height,
                       &dirty)) {
            continue;
        }
        for (int32_t y = 0; y < dirty.height; y++) {
            uint32_t* destination_row = destination +
                (uint32_t)(dirty.y + y) * destination_stride +
                (uint32_t)dirty.x;
            const uint32_t* underlay_row = underlay->pixels +
                (uint32_t)(dirty.y + y) * underlay->stride_pixels +
                (uint32_t)dirty.x;
            for (int32_t x = 0; x < dirty.width; x++) {
                destination_row[x] = underlay_row[x] & 0x00FFFFFFu;
            }
        }
        for (uint32_t z = 0; z < table->count; z++) {
            uint32_t slot = table->z_slots[z];
            if (slot >= OS_WINDOW_MAX_WINDOWS) {
                return OS_ERR_BAD_BUFFER;
            }
            const WindowEntry* window = &table->entries[slot];
            const WindowCompositorSource* source = &sources[slot];
            if (!window->active || !window->visible || source->pixels == 0 ||
                source->stride_pixels < window->width) {
                continue;
            }
            OsRect window_rect = window_state_screen_rect(window);
            int64_t left = dirty.x > window_rect.x ? dirty.x : window_rect.x;
            int64_t top = dirty.y > window_rect.y ? dirty.y : window_rect.y;
            int64_t dirty_right = (int64_t)dirty.x + dirty.width;
            int64_t dirty_bottom = (int64_t)dirty.y + dirty.height;
            int64_t window_right = (int64_t)window_rect.x + window_rect.width;
            int64_t window_bottom = (int64_t)window_rect.y + window_rect.height;
            int64_t right = dirty_right < window_right ? dirty_right : window_right;
            int64_t bottom = dirty_bottom < window_bottom ? dirty_bottom : window_bottom;
            if (right <= left || bottom <= top) {
                continue;
            }
            uint32_t source_x = (uint32_t)(left - window->x);
            uint32_t source_y = (uint32_t)(top - window->y);
            uint32_t copy_width = (uint32_t)(right - left);
            uint32_t copy_height = (uint32_t)(bottom - top);
            for (uint32_t y = 0; y < copy_height; y++) {
                uint32_t* destination_row = destination +
                    (uint32_t)(top + y) * destination_stride + (uint32_t)left;
                const uint32_t* source_row = source->pixels +
                    (source_y + y) * source->stride_pixels + source_x;
                for (uint32_t x = 0; x < copy_width; x++) {
                    destination_row[x] = source_row[x] & 0x00FFFFFFu;
                }
            }
        }
    }
    return OS_SUCCESS;
}

long window_compositor_copy_full(uint32_t* destination,
                                 uint32_t destination_stride,
                                 const uint32_t* source,
                                 uint32_t source_stride,
                                 uint32_t width,
                                 uint32_t height) {
    if (destination == 0 || source == 0 || width == 0 || height == 0 ||
        destination_stride < width || source_stride < width) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t y = 0; y < height; y++) {
        uint32_t* destination_row = destination + y * destination_stride;
        const uint32_t* source_row = source + y * source_stride;
        for (uint32_t x = 0; x < width; x++) {
            destination_row[x] = source_row[x] & 0x00FFFFFFu;
        }
    }
    return OS_SUCCESS;
}

void window_compositor_clear(uint32_t* destination,
                             uint32_t destination_stride,
                             uint32_t width,
                             uint32_t height,
                             uint32_t color) {
    if (destination == 0 || destination_stride < width) {
        return;
    }
    OsRect rect = {0, 0, (int32_t)width, (int32_t)height};
    fill_rect(destination, destination_stride, rect, color);
}
