#ifndef OS64_WINDOWD_STATE_H
#define OS64_WINDOWD_STATE_H

#include <stdint.h>

#include <os64/process_types.h>
#include <os64/window_types.h>

typedef struct WindowEntry {
    uint32_t active;
    uint32_t window_id;
    uint32_t window_generation;
    uint32_t accepted_content_generation;
    uint32_t visible;
    uint32_t layer;
    uint32_t decorated;
    uint32_t minimized;
    uint32_t maximized;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    OsProcessIdentity owner;
} WindowEntry;

typedef struct WindowTable {
    WindowEntry entries[OS_WINDOW_MAX_WINDOWS];
    uint8_t z_slots[OS_WINDOW_MAX_WINDOWS];
    uint32_t count;
} WindowTable;

void window_state_init(WindowTable* table);
long window_state_can_create(const WindowTable* table,
                             OsProcessIdentity owner,
                             uint32_t content_generation,
                             uint32_t width,
                             uint32_t height);
WindowEntry* window_state_commit_create(WindowTable* table,
                                        OsProcessIdentity owner,
                                        uint32_t content_generation,
                                        int32_t x,
                                        int32_t y,
                                        uint32_t width,
                                        uint32_t height);
WindowEntry* window_state_commit_create_layer(WindowTable* table,
                                              OsProcessIdentity owner,
                                              uint32_t content_generation,
                                              int32_t x,
                                              int32_t y,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t layer);
WindowEntry* window_state_find(WindowTable* table,
                               uint32_t window_id,
                               uint32_t window_generation);
const WindowEntry* window_state_find_const(const WindowTable* table,
                                           uint32_t window_id,
                                           uint32_t window_generation);
long window_state_validate_target(WindowTable* table,
                                  OsProcessIdentity sender,
                                  uint32_t window_id,
                                  uint32_t window_generation,
                                  WindowEntry** entry);
long window_state_validate_content(WindowTable* table,
                                   OsProcessIdentity sender,
                                   uint32_t window_id,
                                   uint32_t window_generation,
                                   uint32_t content_generation,
                                   WindowEntry** entry);
void window_state_commit_content(WindowEntry* entry,
                                 uint32_t content_generation);
void window_state_raise(WindowTable* table, WindowEntry* entry);
void window_state_set_visible(WindowTable* table,
                              WindowEntry* entry,
                              uint32_t visible,
                              uint32_t raise);
void window_state_move(WindowEntry* entry, int32_t x, int32_t y);
void window_state_resize(WindowEntry* entry, uint32_t width, uint32_t height);
void window_state_destroy(WindowTable* table, WindowEntry* entry);
uint32_t window_state_slot(const WindowTable* table, const WindowEntry* entry);
uint32_t window_state_layer_from_flags(uint32_t flags);
int window_state_layer_flags_valid(uint32_t flags);
int window_state_accepts_focus(const WindowEntry* entry);
void window_state_set_decorated(WindowEntry* entry, uint32_t decorated);
OsRect window_state_frame_rect(const WindowEntry* entry);

#endif
