#ifndef OS64_WINDOWD_COMPOSITOR_H
#define OS64_WINDOWD_COMPOSITOR_H

#include <stdint.h>

#include <os64/graphics_types.h>
#include <os64/window_types.h>

#include "window_state.h"

#define WINDOW_SCREEN_DAMAGE_MAX_RECTS 64u

typedef struct WindowDamageAccumulator {
    OsRect rects[WINDOW_SCREEN_DAMAGE_MAX_RECTS];
    uint32_t count;
    uint32_t full_screen;
    uint32_t screen_width;
    uint32_t screen_height;
} WindowDamageAccumulator;

typedef struct WindowCompositorSource {
    const uint32_t* pixels;
    uint32_t stride_pixels;
} WindowCompositorSource;

void window_damage_init(WindowDamageAccumulator* damage,
                        uint32_t screen_width,
                        uint32_t screen_height);
void window_damage_reset(WindowDamageAccumulator* damage);
void window_damage_full(WindowDamageAccumulator* damage);
long window_damage_add_screen(WindowDamageAccumulator* damage, OsRect rect);
long window_damage_add_window(WindowDamageAccumulator* damage,
                              const WindowEntry* window,
                              OsRect surface_rect);
OsRect window_state_screen_rect(const WindowEntry* window);

long window_compositor_compose(uint32_t* destination,
                               uint32_t destination_stride,
                               uint32_t screen_width,
                               uint32_t screen_height,
                               uint32_t background,
                               const WindowTable* table,
                               const WindowCompositorSource* sources,
                               const WindowDamageAccumulator* damage);
long window_compositor_copy_full(uint32_t* destination,
                                 uint32_t destination_stride,
                                 const uint32_t* source,
                                 uint32_t source_stride,
                                 uint32_t width,
                                 uint32_t height);
void window_compositor_clear(uint32_t* destination,
                             uint32_t destination_stride,
                             uint32_t width,
                             uint32_t height,
                             uint32_t color);

#endif
