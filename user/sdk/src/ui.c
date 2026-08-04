#include "os64/os64.h"
#include "os64/ui.h"

#include <limits.h>

static int context_valid(const OsUiContext* context) {
    return context != 0 && context->size == sizeof(*context) &&
           context->abi_version == OS64_UI_ABI_VERSION &&
           context->count <= OS_UI_MAX_WIDGETS;
}

static int widget_visible(const OsUiWidget* widget) {
    return widget != 0 && (widget->flags & OS_UI_FLAG_VISIBLE) != 0;
}

static int widget_enabled(const OsUiWidget* widget) {
    return widget != 0 && (widget->flags & OS_UI_FLAG_ENABLED) != 0;
}

static int widget_focusable(const OsUiWidget* widget) {
    return widget_visible(widget) && widget_enabled(widget) &&
           (widget->flags & OS_UI_FLAG_FOCUSABLE) != 0;
}

static int point_in(OsRect rect, int32_t x, int32_t y) {
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
           (int64_t)x < (int64_t)rect.x + rect.width &&
           (int64_t)y < (int64_t)rect.y + rect.height;
}

static int rect_valid(OsRect rect) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return rect.width >= 0 && rect.height >= 0 && right <= INT32_MAX &&
           right >= INT32_MIN && bottom <= INT32_MAX && bottom >= INT32_MIN;
}

static void add_damage(OsUiContext* context, OsRect rect) {
    if (!context_valid(context) || rect.width <= 0 || rect.height <= 0) return;
    if (context->damage_count >= OS_UI_MAX_DAMAGE) {
        context->damage_count = 0;
        context->full_repaint = 1;
        return;
    }
    context->damage[context->damage_count++] = rect;
}

void os_ui_theme_default(OsUiTheme* theme) {
    if (theme == 0) return;
    theme->window = OS_RGB(24, 28, 38);
    theme->surface = OS_RGB(35, 41, 55);
    theme->control = OS_RGB(54, 63, 82);
    theme->control_hover = OS_RGB(68, 79, 102);
    theme->accent = OS_RGB(72, 143, 240);
    theme->text = OS_RGB(238, 243, 252);
    theme->disabled_text = OS_RGB(130, 139, 155);
    theme->border = OS_RGB(91, 103, 126);
}

long os_ui_init(OsUiContext* context, const OsUiTheme* theme) {
    if (context == 0) return OS_ERR_INVALID_ARGUMENT;
    os_memset(context, 0, sizeof(*context));
    context->size = sizeof(*context);
    context->abi_version = OS64_UI_ABI_VERSION;
    context->focused = OS_UI_NONE;
    context->active = OS_UI_NONE;
    if (theme != 0) context->theme = *theme;
    else os_ui_theme_default(&context->theme);
    return OS_SUCCESS;
}

long os_ui_add(OsUiContext* context,
               uint32_t type,
               uint32_t id,
               uint32_t parent_index,
               OsRect rect,
               uint32_t flags,
               uint32_t* index_out) {
    if (!context_valid(context) || type < OS_UI_WIDGET_CONTAINER ||
        type > OS_UI_WIDGET_SCROLL || id == 0 || !rect_valid(rect) ||
        context->count >= OS_UI_MAX_WIDGETS ||
        (parent_index != OS_UI_NONE && parent_index >= context->count))
        return OS_ERR_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < context->count; i++)
        if (context->widgets[i].id == id) return OS_ERR_ALREADY_EXISTS;
    uint32_t index = context->count++;
    OsUiWidget* widget = &context->widgets[index];
    os_memset(widget, 0, sizeof(*widget));
    widget->id = id;
    widget->type = type;
    widget->flags = flags;
    widget->rect = rect;
    widget->parent = parent_index;
    widget->first_child = OS_UI_NONE;
    widget->next_sibling = OS_UI_NONE;
    widget->maximum = 100;
    if (parent_index != OS_UI_NONE) {
        OsUiWidget* parent = &context->widgets[parent_index];
        if (parent->first_child == OS_UI_NONE) parent->first_child = index;
        else {
            uint32_t sibling = parent->first_child;
            while (context->widgets[sibling].next_sibling != OS_UI_NONE)
                sibling = context->widgets[sibling].next_sibling;
            context->widgets[sibling].next_sibling = index;
        }
    }
    add_damage(context, rect);
    if (index_out != 0) *index_out = index;
    return OS_SUCCESS;
}

long os_ui_set_text(OsUiContext* context, uint32_t index, const char* text) {
    if (!context_valid(context) || index >= context->count || text == 0)
        return OS_ERR_INVALID_ARGUMENT;
    uint32_t length = 0;
    while (text[length] != '\0' && length + 1u < OS_UI_TEXT_CAPACITY) length++;
    if (text[length] != '\0') return OS_ERR_BUFFER_TOO_SMALL;
    os_memcpy(context->widgets[index].text, text, length);
    context->widgets[index].text[length] = '\0';
    add_damage(context, context->widgets[index].rect);
    return OS_SUCCESS;
}

long os_ui_set_value(OsUiContext* context, uint32_t index, int32_t value) {
    if (!context_valid(context) || index >= context->count) return OS_ERR_INVALID_ARGUMENT;
    OsUiWidget* widget = &context->widgets[index];
    if (value < widget->minimum) value = widget->minimum;
    if (value > widget->maximum) value = widget->maximum;
    widget->value = value;
    if (widget->type == OS_UI_WIDGET_CHECKBOX) {
        if (value) widget->flags |= OS_UI_FLAG_CHECKED;
        else widget->flags &= ~OS_UI_FLAG_CHECKED;
    }
    add_damage(context, widget->rect);
    return OS_SUCCESS;
}

long os_ui_set_rect(OsUiContext* context, uint32_t index, OsRect rect) {
    if (!context_valid(context) || index >= context->count || !rect_valid(rect)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    add_damage(context, context->widgets[index].rect);
    context->widgets[index].rect = rect;
    add_damage(context, rect);
    return OS_SUCCESS;
}

long os_ui_layout(OsUiContext* context, uint32_t container_index) {
    if (!context_valid(context) || container_index >= context->count ||
        context->widgets[container_index].type != OS_UI_WIDGET_CONTAINER)
        return OS_ERR_INVALID_ARGUMENT;
    OsUiWidget* container = &context->widgets[container_index];
    uint32_t children = 0;
    for (uint32_t at = container->first_child; at != OS_UI_NONE;
         at = context->widgets[at].next_sibling) {
        if (at >= context->count) return OS_ERR_BAD_BUFFER;
        children++;
    }
    if (children == 0) return OS_SUCCESS;
    int vertical = (container->flags & OS_UI_FLAG_VERTICAL) != 0;
    int32_t available = vertical ? container->rect.height : container->rect.width;
    if (available < 0) return OS_ERR_OUT_OF_RANGE;
    int32_t each = available / (int32_t)children;
    int32_t cursor = vertical ? container->rect.y : container->rect.x;
    uint32_t position = 0;
    for (uint32_t at = container->first_child; at != OS_UI_NONE;
         at = context->widgets[at].next_sibling) {
        OsUiWidget* child = &context->widgets[at];
        add_damage(context, child->rect);
        int32_t extent = position + 1u == children
            ? (vertical ? container->rect.y + container->rect.height
                        : container->rect.x + container->rect.width) - cursor
            : each;
        if (vertical)
            child->rect = (OsRect){container->rect.x, cursor,
                                   container->rect.width, extent};
        else
            child->rect = (OsRect){cursor, container->rect.y,
                                   extent, container->rect.height};
        cursor += extent;
        position++;
        add_damage(context, child->rect);
    }
    return OS_SUCCESS;
}

static void draw_border(OsSurfaceCanvas* canvas, OsRect rect, uint32_t color) {
    if (rect.width <= 0 || rect.height <= 0) return;
    os_surface_canvas_fill_rect(canvas, (OsRect){rect.x, rect.y, rect.width, 1}, color);
    os_surface_canvas_fill_rect(canvas, (OsRect){rect.x, rect.y + rect.height - 1,
                                                 rect.width, 1}, color);
    os_surface_canvas_fill_rect(canvas, (OsRect){rect.x, rect.y, 1, rect.height}, color);
    os_surface_canvas_fill_rect(canvas, (OsRect){rect.x + rect.width - 1, rect.y,
                                                 1, rect.height}, color);
}

static void draw_widget(OsUiContext* context,
                        OsSurfaceCanvas* canvas,
                        uint32_t index) {
    OsUiWidget* widget = &context->widgets[index];
    if (!widget_visible(widget) || widget->rect.width <= 0 || widget->rect.height <= 0)
        return;
    int64_t widget_right = (int64_t)widget->rect.x + widget->rect.width;
    int64_t widget_bottom = (int64_t)widget->rect.y + widget->rect.height;
    if (widget_right <= 0 || widget_bottom <= 0 ||
        widget->rect.x >= (int32_t)canvas->width ||
        widget->rect.y >= (int32_t)canvas->height)
        return;
    uint32_t foreground = widget_enabled(widget)
        ? context->theme.text : context->theme.disabled_text;
    uint32_t background = context->theme.surface;
    if (widget->type == OS_UI_WIDGET_CONTAINER ||
        widget->type == OS_UI_WIDGET_SCROLL)
        background = context->theme.window;
    else if (widget->type != OS_UI_WIDGET_LABEL)
        background = index == context->active
            ? context->theme.control_hover : context->theme.control;
    os_surface_canvas_fill_rect(canvas, widget->rect, background);
    if (widget->type != OS_UI_WIDGET_LABEL &&
        widget->type != OS_UI_WIDGET_CONTAINER)
        draw_border(canvas, widget->rect,
                    index == context->focused ? context->theme.accent
                                              : context->theme.border);
    int32_t text_x = widget->rect.x + 6;
    int32_t text_y = widget->rect.y + (widget->rect.height > 8
        ? (widget->rect.height - 7) / 2 : 0);
    if (widget->type == OS_UI_WIDGET_CHECKBOX) {
        OsRect box = {widget->rect.x + 4, widget->rect.y + 4, 12, 12};
        os_surface_canvas_fill_rect(canvas, box,
            (widget->flags & OS_UI_FLAG_CHECKED) != 0
                ? context->theme.accent : context->theme.surface);
        draw_border(canvas, box, context->theme.border);
        text_x += 14;
    }
    os_surface_canvas_draw_text(canvas, text_x, text_y, widget->text,
                                foreground, background,
                                OS_SURFACE_TEXT_TRANSPARENT_BG);
    if (widget->type == OS_UI_WIDGET_TEXT_FIELD &&
        index == context->focused && widget_enabled(widget)) {
        uint32_t bytes = 0;
        while (widget->text[bytes] != '\0') bytes++;
        uint32_t at = 0;
        uint32_t columns = 0;
        while (at < bytes) {
            uint32_t codepoint;
            if (os_utf8_next(widget->text, bytes, &at, &codepoint) < 0) break;
            if (codepoint == '\n') break;
            columns++;
        }
        int64_t caret64 = (int64_t)text_x + (int64_t)columns * 6;
        int64_t right64 = (int64_t)widget->rect.x + widget->rect.width - 3;
        int64_t top64 = (int64_t)widget->rect.y + 4;
        int64_t bottom64 = (int64_t)widget->rect.y + widget->rect.height - 5;
        int64_t selected_caret = caret64 > right64 ? right64 : caret64;
        if (widget->rect.width >= 5 && widget->rect.height >= 9 &&
            selected_caret >= INT32_MIN && selected_caret <= INT32_MAX &&
            top64 >= INT32_MIN && bottom64 <= INT32_MAX &&
            selected_caret >= (int64_t)widget->rect.x + 2 && bottom64 >= top64)
            os_surface_canvas_draw_line(canvas, (int32_t)selected_caret,
                                        (int32_t)top64,
                                        (int32_t)selected_caret,
                                        (int32_t)bottom64,
                                        context->theme.accent);
    }
}

long os_ui_draw(OsUiContext* context, OsSurfaceCanvas* canvas) {
    if (!context_valid(context) || canvas == 0 || canvas->pixels == 0)
        return OS_ERR_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < context->count; i++) draw_widget(context, canvas, i);
    os_ui_damage_reset(context);
    return OS_SUCCESS;
}

static uint32_t hit_test(const OsUiContext* context, int32_t x, int32_t y) {
    for (uint32_t i = context->count; i != 0; i--) {
        const OsUiWidget* widget = &context->widgets[i - 1u];
        if (widget_enabled(widget) && widget_visible(widget) &&
            widget->type != OS_UI_WIDGET_CONTAINER && point_in(widget->rect, x, y))
            return i - 1u;
    }
    return OS_UI_NONE;
}

static void set_focus(OsUiContext* context, uint32_t index) {
    if (context->focused == index) return;
    if (context->focused != OS_UI_NONE)
        add_damage(context, context->widgets[context->focused].rect);
    context->focused = index;
    if (index != OS_UI_NONE) add_damage(context, context->widgets[index].rect);
}

static void focus_next(OsUiContext* context) {
    uint32_t start = context->focused == OS_UI_NONE
        ? 0 : (context->focused + 1u) % context->count;
    for (uint32_t i = 0; i < context->count; i++) {
        uint32_t index = (start + i) % context->count;
        if (widget_focusable(&context->widgets[index])) {
            set_focus(context, index);
            return;
        }
    }
}

static void activate(OsUiContext* context,
                     uint32_t index,
                     OsUiEventResult* result) {
    OsUiWidget* widget = &context->widgets[index];
    result->action = OS_UI_ACTION_ACTIVATE;
    result->widget_id = widget->id;
    result->value = widget->value;
    if (widget->type == OS_UI_WIDGET_CHECKBOX) {
        widget->flags ^= OS_UI_FLAG_CHECKED;
        widget->value = (widget->flags & OS_UI_FLAG_CHECKED) != 0;
        result->action = OS_UI_ACTION_CHANGE;
        result->value = widget->value;
    }
    add_damage(context, widget->rect);
}

long os_ui_dispatch(OsUiContext* context,
                    const OsWindowEvent* event,
                    OsUiEventResult* result) {
    if (!context_valid(context) || event == 0 || result == 0)
        return OS_ERR_INVALID_ARGUMENT;
    result->action = OS_UI_ACTION_NONE;
    result->widget_id = 0;
    result->value = 0;
    if (event->command == OS_WINDOW_EVENT_POINTER &&
        event->input.type == OS_INPUT_EVENT_POINTER) {
        const OsPointerEvent* pointer = &event->input.data.pointer;
        uint32_t hit = hit_test(context, pointer->x, pointer->y);
        if (pointer->type == OS_POINTER_EVENT_BUTTON_DOWN &&
            (pointer->changed_buttons & OS_POINTER_BUTTON_LEFT) != 0) {
            context->active = hit;
            if (hit != OS_UI_NONE) {
                if (widget_focusable(&context->widgets[hit])) set_focus(context, hit);
                add_damage(context, context->widgets[hit].rect);
            }
        } else if (pointer->type == OS_POINTER_EVENT_BUTTON_UP &&
                   (pointer->changed_buttons & OS_POINTER_BUTTON_LEFT) != 0) {
            uint32_t active = context->active;
            context->active = OS_UI_NONE;
            if (active != OS_UI_NONE) add_damage(context, context->widgets[active].rect);
            if (active == hit && hit != OS_UI_NONE) activate(context, hit, result);
        } else if (pointer->type == OS_POINTER_EVENT_WHEEL && hit != OS_UI_NONE &&
                   context->widgets[hit].type == OS_UI_WIDGET_SCROLL) {
            OsUiWidget* widget = &context->widgets[hit];
            os_ui_set_value(context, hit, widget->value - pointer->wheel_delta);
            result->action = OS_UI_ACTION_CHANGE;
            result->widget_id = widget->id;
            result->value = widget->value;
        }
        return OS_SUCCESS;
    }
    if (event->command != OS_WINDOW_EVENT_KEY ||
        event->input.type != OS_INPUT_EVENT_KEY ||
        event->input.data.key.type != OS_KEY_EVENT_DOWN) return OS_SUCCESS;
    const OsKeyEvent* key = &event->input.data.key;
    if (key->keycode == OS_KEY_TAB) {
        focus_next(context);
        result->action = OS_UI_ACTION_FOCUS;
        result->widget_id = context->focused == OS_UI_NONE
            ? 0 : context->widgets[context->focused].id;
        return OS_SUCCESS;
    }
    if (context->focused == OS_UI_NONE) return OS_SUCCESS;
    OsUiWidget* widget = &context->widgets[context->focused];
    if (key->keycode == OS_KEY_ENTER || key->keycode == OS_KEY_SPACE) {
        activate(context, context->focused, result);
    } else if (widget->type == OS_UI_WIDGET_TEXT_FIELD) {
        uint32_t length = 0;
        while (widget->text[length] != '\0') length++;
        if (key->keycode == 0x00Eu && length != 0) widget->text[--length] = '\0';
        else if (key->character >= 0x20u && key->character <= 0x7Eu &&
                 length + 1u < OS_UI_TEXT_CAPACITY) {
            widget->text[length++] = (char)key->character;
            widget->text[length] = '\0';
        } else return OS_SUCCESS;
        add_damage(context, widget->rect);
        result->action = OS_UI_ACTION_CHANGE;
        result->widget_id = widget->id;
        result->value = (int32_t)length;
    }
    return OS_SUCCESS;
}

uint32_t os_ui_focused(const OsUiContext* context) {
    return context_valid(context) ? context->focused : OS_UI_NONE;
}

void os_ui_damage_reset(OsUiContext* context) {
    if (!context_valid(context)) return;
    context->damage_count = 0;
    context->full_repaint = 0;
}
