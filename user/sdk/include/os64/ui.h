#ifndef OS64_UI_H
#define OS64_UI_H

#include <stdint.h>

#include "os64/surface.h"
#include "os64/window_types.h"

#define OS64_UI_ABI_VERSION 1u
#define OS_UI_MAX_WIDGETS 64u
#define OS_UI_MAX_DAMAGE 16u
#define OS_UI_TEXT_CAPACITY 96u
#define OS_UI_NONE UINT32_MAX

#define OS_UI_WIDGET_CONTAINER 1u
#define OS_UI_WIDGET_LABEL 2u
#define OS_UI_WIDGET_BUTTON 3u
#define OS_UI_WIDGET_TEXT_FIELD 4u
#define OS_UI_WIDGET_CHECKBOX 5u
#define OS_UI_WIDGET_LIST 6u
#define OS_UI_WIDGET_MENU 7u
#define OS_UI_WIDGET_SCROLL 8u

#define OS_UI_FLAG_VISIBLE (1u << 0)
#define OS_UI_FLAG_ENABLED (1u << 1)
#define OS_UI_FLAG_FOCUSABLE (1u << 2)
#define OS_UI_FLAG_CHECKED (1u << 3)
#define OS_UI_FLAG_VERTICAL (1u << 4)

#define OS_UI_ACTION_NONE 0u
#define OS_UI_ACTION_ACTIVATE 1u
#define OS_UI_ACTION_CHANGE 2u
#define OS_UI_ACTION_FOCUS 3u

typedef struct OsUiTheme {
    uint32_t window;
    uint32_t surface;
    uint32_t control;
    uint32_t control_hover;
    uint32_t accent;
    uint32_t text;
    uint32_t disabled_text;
    uint32_t border;
} OsUiTheme;

typedef struct OsUiWidget {
    uint32_t id;
    uint32_t type;
    uint32_t flags;
    OsRect rect;
    uint32_t parent;
    uint32_t first_child;
    uint32_t next_sibling;
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    char text[OS_UI_TEXT_CAPACITY];
} OsUiWidget;

typedef struct OsUiEventResult {
    uint32_t action;
    uint32_t widget_id;
    int32_t value;
} OsUiEventResult;

typedef struct OsUiContext {
    uint32_t size;
    uint32_t abi_version;
    OsUiTheme theme;
    OsUiWidget widgets[OS_UI_MAX_WIDGETS];
    uint32_t count;
    uint32_t focused;
    uint32_t active;
    OsRect damage[OS_UI_MAX_DAMAGE];
    uint32_t damage_count;
    uint32_t full_repaint;
} OsUiContext;

void os_ui_theme_default(OsUiTheme* theme);
long os_ui_init(OsUiContext* context, const OsUiTheme* theme);
long os_ui_add(OsUiContext* context,
               uint32_t type,
               uint32_t id,
               uint32_t parent_index,
               OsRect rect,
               uint32_t flags,
               uint32_t* index_out);
long os_ui_set_text(OsUiContext* context, uint32_t index, const char* text);
long os_ui_set_value(OsUiContext* context, uint32_t index, int32_t value);
long os_ui_set_rect(OsUiContext* context, uint32_t index, OsRect rect);
long os_ui_layout(OsUiContext* context, uint32_t container_index);
long os_ui_draw(OsUiContext* context, OsSurfaceCanvas* canvas);
long os_ui_dispatch(OsUiContext* context,
                    const OsWindowEvent* event,
                    OsUiEventResult* result);
uint32_t os_ui_focused(const OsUiContext* context);
void os_ui_damage_reset(OsUiContext* context);

#endif
