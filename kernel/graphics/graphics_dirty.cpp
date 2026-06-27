#include "kernel/graphics/graphics2d.h"

static void zero_rect(OsRect* rect) {
    if (rect == 0) {
        return;
    }
    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
}

static void reset_tracker(GraphicsDirtyTracker* tracker) {
    if (tracker == 0) {
        return;
    }
    zero_rect(&tracker->bounds);
    for (uint32_t i = 0; i < GFX_DIRTY_MAX_RECTS; i++) {
        zero_rect(&tracker->rects[i]);
    }
    tracker->count = 0;
    tracker->full = 0;
}

static int rects_touch_or_overlap(const OsRect* a, const OsRect* b) {
    int64_t a_left = a->x;
    int64_t a_top = a->y;
    int64_t a_right = a_left + a->width;
    int64_t a_bottom = a_top + a->height;
    int64_t b_left = b->x;
    int64_t b_top = b->y;
    int64_t b_right = b_left + b->width;
    int64_t b_bottom = b_top + b->height;

    return a_left <= b_right &&
           b_left <= a_right &&
           a_top <= b_bottom &&
           b_top <= a_bottom;
}

static OsRect rect_union(const OsRect* a, const OsRect* b) {
    int64_t a_left = a->x;
    int64_t a_top = a->y;
    int64_t a_right = a_left + a->width;
    int64_t a_bottom = a_top + a->height;
    int64_t b_left = b->x;
    int64_t b_top = b->y;
    int64_t b_right = b_left + b->width;
    int64_t b_bottom = b_top + b->height;

    int64_t left = a_left < b_left ? a_left : b_left;
    int64_t top = a_top < b_top ? a_top : b_top;
    int64_t right = a_right > b_right ? a_right : b_right;
    int64_t bottom = a_bottom > b_bottom ? a_bottom : b_bottom;

    OsRect output;
    output.x = (int32_t)left;
    output.y = (int32_t)top;
    output.width = (int32_t)(right - left);
    output.height = (int32_t)(bottom - top);
    return output;
}

static void remove_rect(GraphicsDirtyTracker* tracker, uint32_t index) {
    if (tracker == 0 || index >= tracker->count) {
        return;
    }
    for (uint32_t i = index; i + 1 < tracker->count; i++) {
        tracker->rects[i] = tracker->rects[i + 1];
    }
    tracker->count--;
    zero_rect(&tracker->rects[tracker->count]);
}

static void compact_overlaps(GraphicsDirtyTracker* tracker, uint32_t start_index) {
    if (tracker == 0 || start_index >= tracker->count) {
        return;
    }

    uint32_t i = 0;
    while (i < tracker->count) {
        if (i == start_index) {
            i++;
            continue;
        }
        if (!rects_touch_or_overlap(&tracker->rects[start_index], &tracker->rects[i])) {
            i++;
            continue;
        }

        tracker->rects[start_index] =
            rect_union(&tracker->rects[start_index], &tracker->rects[i]);
        remove_rect(tracker, i);
        if (i < start_index) {
            start_index--;
        }
        i = 0;
    }
}

void gfx_dirty_init(GraphicsDirtyTracker* tracker, const OsRect* bounds) {
    reset_tracker(tracker);
    if (tracker == 0 || gfx_rect_is_empty(bounds)) {
        return;
    }
    tracker->bounds = *bounds;
}

void gfx_dirty_clear(GraphicsDirtyTracker* tracker) {
    if (tracker == 0) {
        return;
    }
    for (uint32_t i = 0; i < tracker->count; i++) {
        zero_rect(&tracker->rects[i]);
    }
    tracker->count = 0;
    tracker->full = 0;
}

void gfx_dirty_mark_full(GraphicsDirtyTracker* tracker) {
    if (tracker == 0 || gfx_rect_is_empty(&tracker->bounds)) {
        return;
    }
    tracker->rects[0] = tracker->bounds;
    for (uint32_t i = 1; i < GFX_DIRTY_MAX_RECTS; i++) {
        zero_rect(&tracker->rects[i]);
    }
    tracker->count = 1;
    tracker->full = 1;
}

void gfx_dirty_mark(GraphicsDirtyTracker* tracker, const OsRect* rect) {
    if (tracker == 0 || tracker->full || gfx_rect_is_empty(&tracker->bounds)) {
        return;
    }

    OsRect clipped;
    if (!gfx_clip_rect(&tracker->bounds, rect, &clipped)) {
        return;
    }

    for (uint32_t i = 0; i < tracker->count; i++) {
        if (!rects_touch_or_overlap(&tracker->rects[i], &clipped)) {
            continue;
        }
        tracker->rects[i] = rect_union(&tracker->rects[i], &clipped);
        compact_overlaps(tracker, i);
        return;
    }

    if (tracker->count >= GFX_DIRTY_MAX_RECTS) {
        gfx_dirty_mark_full(tracker);
        return;
    }

    tracker->rects[tracker->count] = clipped;
    tracker->count++;
}

uint32_t gfx_dirty_count(const GraphicsDirtyTracker* tracker) {
    return tracker == 0 ? 0 : tracker->count;
}

uint32_t gfx_dirty_is_full(const GraphicsDirtyTracker* tracker) {
    return tracker == 0 ? 0 : tracker->full;
}

const OsRect* gfx_dirty_rects(const GraphicsDirtyTracker* tracker) {
    return tracker == 0 ? 0 : tracker->rects;
}
