#ifndef OS64_WINDOWD_INTERACTION_H
#define OS64_WINDOWD_INTERACTION_H

#include <os64/input_types.h>

#include "window_state.h"

#define WINDOW_HIT_NONE 0u
#define WINDOW_HIT_CONTENT 1u
#define WINDOW_HIT_TITLE 2u
#define WINDOW_HIT_CLOSE 3u
#define WINDOW_HIT_MINIMIZE 4u
#define WINDOW_HIT_MAXIMIZE 5u
#define WINDOW_HIT_RESIZE_LEFT  (1u << 8)
#define WINDOW_HIT_RESIZE_RIGHT (1u << 9)
#define WINDOW_HIT_RESIZE_TOP   (1u << 10)
#define WINDOW_HIT_RESIZE_BOTTOM (1u << 11)
#define WINDOW_HIT_RESIZE_MASK  (WINDOW_HIT_RESIZE_LEFT | \
                                 WINDOW_HIT_RESIZE_RIGHT | \
                                 WINDOW_HIT_RESIZE_TOP | \
                                 WINDOW_HIT_RESIZE_BOTTOM)

#define WINDOW_CAPTURE_NONE 0u
#define WINDOW_CAPTURE_CLIENT 1u
#define WINDOW_CAPTURE_MOVE 2u
#define WINDOW_CAPTURE_RESIZE 3u
#define WINDOW_CAPTURE_CONTROL 4u

typedef struct WindowHit {
    WindowEntry* entry;
    uint32_t region;
} WindowHit;

typedef struct WindowInteraction {
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t buttons;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t capture_mode;
    uint32_t capture_region;
    uint32_t capture_window_id;
    uint32_t capture_window_generation;
    int32_t anchor_pointer_x;
    int32_t anchor_pointer_y;
    int32_t anchor_x;
    int32_t anchor_y;
    uint32_t anchor_width;
    uint32_t anchor_height;
} WindowInteraction;

void window_interaction_init(WindowInteraction* interaction,
                             uint32_t screen_width,
                             uint32_t screen_height);
long window_interaction_normalize(WindowInteraction* interaction,
                                  const OsPointerEvent* raw,
                                  OsPointerEvent* normalized);
WindowHit window_interaction_hit_test(WindowTable* table, int32_t x, int32_t y);
void window_interaction_capture(WindowInteraction* interaction,
                                const WindowEntry* entry,
                                uint32_t mode,
                                uint32_t region);
void window_interaction_cancel(WindowInteraction* interaction);
void window_interaction_cancel_target(WindowInteraction* interaction,
                                      uint32_t window_id,
                                      uint32_t window_generation);
WindowEntry* window_interaction_capture_target(WindowInteraction* interaction,
                                               WindowTable* table);
int window_interaction_update_geometry(WindowInteraction* interaction,
                                       WindowEntry* entry);

#endif
