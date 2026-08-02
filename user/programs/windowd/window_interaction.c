#include "window_interaction.h"

#include <limits.h>

#include <os64/result.h>

static int point_in(int32_t x, int32_t y, OsRect rect) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
           (int64_t)x < right && (int64_t)y < bottom;
}

void window_interaction_init(WindowInteraction* interaction,
                             uint32_t screen_width,
                             uint32_t screen_height) {
    if (interaction == 0) return;
    interaction->pointer_x = screen_width == 0 ? 0 : (int32_t)(screen_width / 2u);
    interaction->pointer_y = screen_height == 0 ? 0 : (int32_t)(screen_height / 2u);
    interaction->buttons = 0;
    interaction->screen_width = screen_width;
    interaction->screen_height = screen_height;
    window_interaction_cancel(interaction);
}

static int32_t clamp_axis(int64_t value, uint32_t extent) {
    if (extent == 0) return 0;
    if (value < 0) return 0;
    if ((uint64_t)value >= extent) return (int32_t)(extent - 1u);
    return (int32_t)value;
}

long window_interaction_normalize(WindowInteraction* interaction,
                                  const OsPointerEvent* raw,
                                  OsPointerEvent* normalized) {
    if (interaction == 0 || raw == 0 || normalized == 0 ||
        interaction->screen_width == 0 || interaction->screen_height == 0 ||
        raw->type < OS_POINTER_EVENT_MOVE ||
        raw->type > OS_POINTER_EVENT_WHEEL ||
        (raw->buttons & ~(OS_POINTER_BUTTON_LEFT | OS_POINTER_BUTTON_RIGHT |
                         OS_POINTER_BUTTON_MIDDLE | OS_POINTER_BUTTON_X1 |
                         OS_POINTER_BUTTON_X2)) != 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    int64_t x = raw->x == OS_POINTER_POSITION_UNKNOWN
        ? (int64_t)interaction->pointer_x + raw->delta_x : raw->x;
    int64_t y = raw->y == OS_POINTER_POSITION_UNKNOWN
        ? (int64_t)interaction->pointer_y + raw->delta_y : raw->y;
    int32_t old_x = interaction->pointer_x;
    int32_t old_y = interaction->pointer_y;
    interaction->pointer_x = clamp_axis(x, interaction->screen_width);
    interaction->pointer_y = clamp_axis(y, interaction->screen_height);
    interaction->buttons = raw->buttons;
    *normalized = *raw;
    normalized->x = interaction->pointer_x;
    normalized->y = interaction->pointer_y;
    normalized->delta_x = interaction->pointer_x - old_x;
    normalized->delta_y = interaction->pointer_y - old_y;
    return OS_SUCCESS;
}

static uint32_t decorated_region(const WindowEntry* entry, int32_t x, int32_t y) {
    uint32_t b = OS_WINDOW_DECORATION_BORDER;
    uint32_t t = OS_WINDOW_DECORATION_TITLE_HEIGHT;
    OsRect frame = window_state_frame_rect(entry);
    if (!point_in(x, y, frame)) return WINDOW_HIT_NONE;
    uint32_t edges = 0;
    if (x < entry->x) edges |= WINDOW_HIT_RESIZE_LEFT;
    if ((int64_t)x >= (int64_t)entry->x + entry->width)
        edges |= WINDOW_HIT_RESIZE_RIGHT;
    if (y < frame.y + (int32_t)b) edges |= WINDOW_HIT_RESIZE_TOP;
    if ((int64_t)y >= (int64_t)entry->y + entry->height)
        edges |= WINDOW_HIT_RESIZE_BOTTOM;
    if (edges != 0) return edges;
    if (y < entry->y) {
        int32_t button_top = entry->y - (int32_t)t + 3;
        int32_t button_bottom = button_top + (int32_t)OS_WINDOW_DECORATION_BUTTON_SIZE;
        if (y >= button_top && y < button_bottom) {
            int32_t right = entry->x + (int32_t)entry->width - 3;
            int32_t size = (int32_t)OS_WINDOW_DECORATION_BUTTON_SIZE;
            if (x >= right - size) return WINDOW_HIT_CLOSE;
            if (x >= right - size * 2 - 2) return WINDOW_HIT_MAXIMIZE;
            if (x >= right - size * 3 - 4) return WINDOW_HIT_MINIMIZE;
        }
        return WINDOW_HIT_TITLE;
    }
    return WINDOW_HIT_CONTENT;
}

WindowHit window_interaction_hit_test(WindowTable* table, int32_t x, int32_t y) {
    WindowHit hit = {0, WINDOW_HIT_NONE};
    if (table == 0) return hit;
    for (uint32_t z = table->count; z != 0; z--) {
        uint32_t slot = table->z_slots[z - 1u];
        if (slot >= OS_WINDOW_MAX_WINDOWS) continue;
        WindowEntry* entry = &table->entries[slot];
        if (!entry->active || !entry->visible) continue;
        uint32_t region = entry->decorated
            ? decorated_region(entry, x, y)
            : (point_in(x, y, window_state_frame_rect(entry))
                ? WINDOW_HIT_CONTENT : WINDOW_HIT_NONE);
        if (region != WINDOW_HIT_NONE) {
            hit.entry = entry;
            hit.region = region;
            return hit;
        }
    }
    return hit;
}

void window_interaction_capture(WindowInteraction* interaction,
                                const WindowEntry* entry,
                                uint32_t mode,
                                uint32_t region) {
    if (interaction == 0 || entry == 0 || !entry->active) return;
    interaction->capture_mode = mode;
    interaction->capture_region = region;
    interaction->capture_window_id = entry->window_id;
    interaction->capture_window_generation = entry->window_generation;
    interaction->anchor_pointer_x = interaction->pointer_x;
    interaction->anchor_pointer_y = interaction->pointer_y;
    interaction->anchor_x = entry->x;
    interaction->anchor_y = entry->y;
    interaction->anchor_width = entry->width;
    interaction->anchor_height = entry->height;
}

void window_interaction_cancel(WindowInteraction* interaction) {
    if (interaction == 0) return;
    interaction->capture_mode = WINDOW_CAPTURE_NONE;
    interaction->capture_region = WINDOW_HIT_NONE;
    interaction->capture_window_id = 0;
    interaction->capture_window_generation = 0;
    interaction->anchor_pointer_x = 0;
    interaction->anchor_pointer_y = 0;
    interaction->anchor_x = 0;
    interaction->anchor_y = 0;
    interaction->anchor_width = 0;
    interaction->anchor_height = 0;
}

void window_interaction_cancel_target(WindowInteraction* interaction,
                                      uint32_t window_id,
                                      uint32_t window_generation) {
    if (interaction != 0 && interaction->capture_window_id == window_id &&
        interaction->capture_window_generation == window_generation) {
        window_interaction_cancel(interaction);
    }
}

WindowEntry* window_interaction_capture_target(WindowInteraction* interaction,
                                               WindowTable* table) {
    if (interaction == 0 || interaction->capture_mode == WINDOW_CAPTURE_NONE)
        return 0;
    WindowEntry* entry = window_state_find(table, interaction->capture_window_id,
                                           interaction->capture_window_generation);
    if (entry == 0 || !entry->visible) {
        window_interaction_cancel(interaction);
        return 0;
    }
    return entry;
}

static int32_t bounded_i32(int64_t value) {
    if (value < INT32_MIN) return INT32_MIN;
    if (value > INT32_MAX) return INT32_MAX;
    return (int32_t)value;
}

int window_interaction_update_geometry(WindowInteraction* interaction,
                                       WindowEntry* entry) {
    if (interaction == 0 || entry == 0 || !entry->active) return 0;
    int64_t dx = (int64_t)interaction->pointer_x - interaction->anchor_pointer_x;
    int64_t dy = (int64_t)interaction->pointer_y - interaction->anchor_pointer_y;
    if (interaction->capture_mode == WINDOW_CAPTURE_MOVE) {
        int64_t x = (int64_t)interaction->anchor_x + dx;
        int64_t y = (int64_t)interaction->anchor_y + dy;
        int64_t min_x = 32 - (int64_t)entry->width;
        int64_t max_x = (int64_t)interaction->screen_width - 32;
        int64_t min_y = OS_WINDOW_DECORATION_TITLE_HEIGHT;
        int64_t max_y = (int64_t)interaction->screen_height - 8;
        if (x < min_x) x = min_x;
        if (x > max_x) x = max_x;
        if (y < min_y) y = min_y;
        if (y > max_y) y = max_y;
        int changed = entry->x != x || entry->y != y;
        entry->x = bounded_i32(x);
        entry->y = bounded_i32(y);
        return changed;
    }
    if (interaction->capture_mode != WINDOW_CAPTURE_RESIZE) return 0;

    int64_t left = interaction->anchor_x;
    int64_t top = interaction->anchor_y;
    int64_t right = left + interaction->anchor_width;
    int64_t bottom = top + interaction->anchor_height;
    uint32_t edges = interaction->capture_region & WINDOW_HIT_RESIZE_MASK;
    if ((edges & WINDOW_HIT_RESIZE_LEFT) != 0) left += dx;
    if ((edges & WINDOW_HIT_RESIZE_RIGHT) != 0) right += dx;
    if ((edges & WINDOW_HIT_RESIZE_TOP) != 0) top += dy;
    if ((edges & WINDOW_HIT_RESIZE_BOTTOM) != 0) bottom += dy;
    if (right - left < OS_WINDOW_MIN_WIDTH) {
        if ((edges & WINDOW_HIT_RESIZE_LEFT) != 0)
            left = right - OS_WINDOW_MIN_WIDTH;
        else right = left + OS_WINDOW_MIN_WIDTH;
    }
    if (bottom - top < OS_WINDOW_MIN_HEIGHT) {
        if ((edges & WINDOW_HIT_RESIZE_TOP) != 0)
            top = bottom - OS_WINDOW_MIN_HEIGHT;
        else bottom = top + OS_WINDOW_MIN_HEIGHT;
    }
    if (left < -(int64_t)interaction->screen_width) left = -(int64_t)interaction->screen_width;
    if (top < OS_WINDOW_DECORATION_TITLE_HEIGHT) top = OS_WINDOW_DECORATION_TITLE_HEIGHT;
    if (right > (int64_t)interaction->screen_width * 2) right = (int64_t)interaction->screen_width * 2;
    if (bottom > (int64_t)interaction->screen_height * 2) bottom = (int64_t)interaction->screen_height * 2;
    uint32_t width = (uint32_t)(right - left);
    uint32_t height = (uint32_t)(bottom - top);
    int changed = entry->x != left || entry->y != top ||
                  entry->width != width || entry->height != height;
    entry->x = bounded_i32(left);
    entry->y = bounded_i32(top);
    entry->width = width;
    entry->height = height;
    return changed;
}
