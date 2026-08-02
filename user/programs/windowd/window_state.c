#include "window_state.h"

#include <os64/result.h>

static int identity_equal(OsProcessIdentity left, OsProcessIdentity right) {
    return left.pid != 0 && left.pid == right.pid && left.generation != 0 &&
           left.generation == right.generation;
}

int window_state_layer_flags_valid(uint32_t flags) {
    uint32_t selected = flags & OS_WINDOW_FLAG_LAYER_MASK;
    return (flags & ~(OS_WINDOW_FLAG_LAYER_MASK |
                      OS_WINDOW_FLAG_DECORATED)) == 0 &&
           (selected == 0 || (selected & (selected - 1u)) == 0);
}

uint32_t window_state_layer_from_flags(uint32_t flags) {
    if (!window_state_layer_flags_valid(flags)) return UINT32_MAX;
    if ((flags & OS_WINDOW_FLAG_LAYER_DESKTOP) != 0)
        return OS_WINDOW_LAYER_DESKTOP;
    if ((flags & OS_WINDOW_FLAG_LAYER_PANEL) != 0)
        return OS_WINDOW_LAYER_PANEL;
    if ((flags & OS_WINDOW_FLAG_LAYER_SYSTEM_OVERLAY) != 0)
        return OS_WINDOW_LAYER_SYSTEM_OVERLAY;
    return OS_WINDOW_LAYER_NORMAL;
}

int window_state_accepts_focus(const WindowEntry* entry) {
    return entry != 0 && entry->active && entry->visible &&
           entry->layer != OS_WINDOW_LAYER_DESKTOP;
}

uint32_t window_state_slot(const WindowTable* table, const WindowEntry* entry) {
    if (table == 0 || entry == 0 || entry < table->entries ||
        entry >= table->entries + OS_WINDOW_MAX_WINDOWS) {
        return OS_WINDOW_MAX_WINDOWS;
    }
    return (uint32_t)(entry - table->entries);
}

void window_state_init(WindowTable* table) {
    if (table == 0) {
        return;
    }
    table->count = 0;
    for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
        WindowEntry* entry = &table->entries[i];
        entry->active = 0;
        entry->window_id = i + 1u;
        entry->window_generation = 0;
        entry->accepted_content_generation = 0;
        entry->visible = 0;
        entry->layer = OS_WINDOW_LAYER_NORMAL;
        entry->decorated = 0;
        entry->minimized = 0;
        entry->maximized = 0;
        entry->x = 0;
        entry->y = 0;
        entry->width = 0;
        entry->height = 0;
        entry->restore_x = 0;
        entry->restore_y = 0;
        entry->restore_width = 0;
        entry->restore_height = 0;
        entry->owner.pid = 0;
        entry->owner.generation = 0;
        table->z_slots[i] = 0;
    }
}

long window_state_can_create(const WindowTable* table,
                             OsProcessIdentity owner,
                             uint32_t content_generation,
                             uint32_t width,
                             uint32_t height) {
    if (table == 0 || owner.pid == 0 || owner.generation == 0 ||
        content_generation == 0 || width == 0 || height == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return table->count < OS_WINDOW_MAX_WINDOWS
        ? OS_SUCCESS
        : OS_ERR_NO_RESOURCES;
}

WindowEntry* window_state_commit_create(WindowTable* table,
                                        OsProcessIdentity owner,
                                        uint32_t content_generation,
                                        int32_t x,
                                        int32_t y,
                                        uint32_t width,
                                        uint32_t height) {
    return window_state_commit_create_layer(table, owner, content_generation,
                                            x, y, width, height,
                                            OS_WINDOW_LAYER_NORMAL);
}

WindowEntry* window_state_commit_create_layer(WindowTable* table,
                                              OsProcessIdentity owner,
                                              uint32_t content_generation,
                                              int32_t x,
                                              int32_t y,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t layer) {
    if (window_state_can_create(table, owner, content_generation, width, height) < 0) {
        return 0;
    }
    if (layer > OS_WINDOW_LAYER_SYSTEM_OVERLAY) return 0;
    for (uint32_t i = 0; i < OS_WINDOW_MAX_WINDOWS; i++) {
        WindowEntry* entry = &table->entries[i];
        if (entry->active) {
            continue;
        }
        uint32_t generation = entry->window_generation + 1u;
        if (generation == 0) {
            generation = 1;
        }
        entry->active = 1;
        entry->window_generation = generation;
        entry->accepted_content_generation = content_generation;
        entry->visible = 1;
        entry->layer = layer;
        entry->decorated = 0;
        entry->minimized = 0;
        entry->maximized = 0;
        entry->x = x;
        entry->y = y;
        entry->width = width;
        entry->height = height;
        entry->restore_x = x;
        entry->restore_y = y;
        entry->restore_width = width;
        entry->restore_height = height;
        entry->owner = owner;
        uint32_t at = table->count;
        while (at != 0) {
            uint32_t previous_slot = table->z_slots[at - 1u];
            if (previous_slot >= OS_WINDOW_MAX_WINDOWS ||
                table->entries[previous_slot].layer <= layer) break;
            table->z_slots[at] = table->z_slots[at - 1u];
            at--;
        }
        table->z_slots[at] = (uint8_t)i;
        table->count++;
        return entry;
    }
    return 0;
}

WindowEntry* window_state_find(WindowTable* table,
                               uint32_t window_id,
                               uint32_t window_generation) {
    if (table == 0 || window_id == 0 || window_id > OS_WINDOW_MAX_WINDOWS ||
        window_generation == 0) {
        return 0;
    }
    WindowEntry* entry = &table->entries[window_id - 1u];
    return entry->active && entry->window_generation == window_generation
        ? entry
        : 0;
}

const WindowEntry* window_state_find_const(const WindowTable* table,
                                           uint32_t window_id,
                                           uint32_t window_generation) {
    return window_state_find((WindowTable*)table, window_id, window_generation);
}

long window_state_validate_target(WindowTable* table,
                                  OsProcessIdentity sender,
                                  uint32_t window_id,
                                  uint32_t window_generation,
                                  WindowEntry** entry) {
    WindowEntry* found = window_state_find(table, window_id, window_generation);
    if (entry != 0) {
        *entry = found;
    }
    if (found == 0) {
        return OS_ERR_NOT_FOUND;
    }
    return identity_equal(found->owner, sender)
        ? OS_SUCCESS
        : OS_ERR_PERMISSION_DENIED;
}

long window_state_validate_content(WindowTable* table,
                                   OsProcessIdentity sender,
                                   uint32_t window_id,
                                   uint32_t window_generation,
                                   uint32_t content_generation,
                                   WindowEntry** entry) {
    WindowEntry* found = 0;
    long result = window_state_validate_target(table,
                                               sender,
                                               window_id,
                                               window_generation,
                                               &found);
    if (entry != 0) {
        *entry = found;
    }
    if (result < 0) {
        return result;
    }
    if (content_generation == 0 ||
        content_generation <= found->accepted_content_generation) {
        return OS_ERR_ALREADY_EXISTS;
    }
    return OS_SUCCESS;
}

void window_state_commit_content(WindowEntry* entry,
                                 uint32_t content_generation) {
    if (entry != 0 && entry->active) {
        entry->accepted_content_generation = content_generation;
    }
}

void window_state_raise(WindowTable* table, WindowEntry* entry) {
    uint32_t slot = window_state_slot(table, entry);
    if (slot >= OS_WINDOW_MAX_WINDOWS || !entry->active || table->count == 0) {
        return;
    }
    uint32_t at = table->count;
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->z_slots[i] == slot) {
            at = i;
            break;
        }
    }
    if (at >= table->count) {
        return;
    }
    uint32_t layer_end = at + 1u;
    while (layer_end < table->count) {
        uint32_t next_slot = table->z_slots[layer_end];
        if (next_slot >= OS_WINDOW_MAX_WINDOWS ||
            table->entries[next_slot].layer != entry->layer) break;
        layer_end++;
    }
    if (at + 1u == layer_end) return;
    for (uint32_t i = at; i + 1u < layer_end; i++) {
        table->z_slots[i] = table->z_slots[i + 1u];
    }
    table->z_slots[layer_end - 1u] = (uint8_t)slot;
}

void window_state_set_visible(WindowTable* table,
                              WindowEntry* entry,
                              uint32_t visible,
                              uint32_t raise) {
    if (entry == 0 || !entry->active) {
        return;
    }
    entry->visible = visible ? 1u : 0u;
    if (entry->visible) entry->minimized = 0;
    if (entry->visible && raise) {
        window_state_raise(table, entry);
    }
}

void window_state_set_decorated(WindowEntry* entry, uint32_t decorated) {
    if (entry != 0 && entry->active) {
        entry->decorated = decorated ? 1u : 0u;
    }
}

OsRect window_state_frame_rect(const WindowEntry* entry) {
    OsRect rect = {0, 0, 0, 0};
    if (entry == 0 || !entry->active || entry->width > INT32_MAX ||
        entry->height > INT32_MAX) return rect;
    rect.x = entry->x;
    rect.y = entry->y;
    rect.width = (int32_t)entry->width;
    rect.height = (int32_t)entry->height;
    if (entry->decorated) {
        rect.x -= (int32_t)OS_WINDOW_DECORATION_BORDER;
        rect.y -= (int32_t)OS_WINDOW_DECORATION_TITLE_HEIGHT;
        rect.width += (int32_t)(OS_WINDOW_DECORATION_BORDER * 2u);
        rect.height += (int32_t)(OS_WINDOW_DECORATION_TITLE_HEIGHT +
                                 OS_WINDOW_DECORATION_BORDER);
    }
    return rect;
}

void window_state_move(WindowEntry* entry, int32_t x, int32_t y) {
    if (entry != 0 && entry->active) {
        entry->x = x;
        entry->y = y;
    }
}

void window_state_resize(WindowEntry* entry, uint32_t width, uint32_t height) {
    if (entry != 0 && entry->active && width != 0 && height != 0) {
        entry->width = width;
        entry->height = height;
    }
}

void window_state_destroy(WindowTable* table, WindowEntry* entry) {
    uint32_t slot = window_state_slot(table, entry);
    if (slot >= OS_WINDOW_MAX_WINDOWS || !entry->active) {
        return;
    }
    uint32_t at = table->count;
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->z_slots[i] == slot) {
            at = i;
            break;
        }
    }
    if (at < table->count) {
        for (uint32_t i = at; i + 1u < table->count; i++) {
            table->z_slots[i] = table->z_slots[i + 1u];
        }
        table->count--;
    }
    entry->active = 0;
    entry->accepted_content_generation = 0;
    entry->visible = 0;
    entry->layer = OS_WINDOW_LAYER_NORMAL;
    entry->decorated = 0;
    entry->minimized = 0;
    entry->maximized = 0;
    entry->x = 0;
    entry->y = 0;
    entry->width = 0;
    entry->height = 0;
    entry->restore_x = 0;
    entry->restore_y = 0;
    entry->restore_width = 0;
    entry->restore_height = 0;
    entry->owner.pid = 0;
    entry->owner.generation = 0;
}
