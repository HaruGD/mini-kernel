#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <os64/os64.h>

void* os_memset(void* out, int value, size_t size) { return memset(out, value, size); }
void* os_memcpy(void* out, const void* in, size_t size) { return memcpy(out, in, size); }

static OsWindowEvent pointer(uint32_t type, int32_t x, int32_t y,
                             uint32_t buttons, uint32_t changed) {
    OsWindowEvent event = {0};
    event.command = OS_WINDOW_EVENT_POINTER;
    event.input.type = OS_INPUT_EVENT_POINTER;
    event.input.data.pointer.type = type;
    event.input.data.pointer.x = x;
    event.input.data.pointer.y = y;
    event.input.data.pointer.buttons = buttons;
    event.input.data.pointer.changed_buttons = changed;
    return event;
}

static OsWindowEvent key(uint32_t keycode, uint32_t character) {
    OsWindowEvent event = {0};
    event.command = OS_WINDOW_EVENT_KEY;
    event.input.type = OS_INPUT_EVENT_KEY;
    event.input.data.key.type = OS_KEY_EVENT_DOWN;
    event.input.data.key.keycode = keycode;
    event.input.data.key.character = character;
    return event;
}

static void click(OsUiContext* ui, uint32_t index, OsUiEventResult* result) {
    OsRect rect = ui->widgets[index].rect;
    OsWindowEvent event = pointer(OS_POINTER_EVENT_BUTTON_DOWN,
                                  rect.x + 1, rect.y + 1,
                                  OS_POINTER_BUTTON_LEFT,
                                  OS_POINTER_BUTTON_LEFT);
    assert(os_ui_dispatch(ui, &event, result) == 0);
    event = pointer(OS_POINTER_EVENT_BUTTON_UP, rect.x + 1, rect.y + 1,
                    0, OS_POINTER_BUTTON_LEFT);
    assert(os_ui_dispatch(ui, &event, result) == 0);
}

int main(void) {
    uint32_t storage[64 * 48 + 2];
    storage[0] = 0xDEADBEEFu;
    storage[64 * 48 + 1] = 0xC001D00Du;
    OsGraphicsSurfaceHandleInfo info = {0};
    info.width = 64; info.height = 48; info.stride_pixels = 64;
    info.pixel_format = OS64_PIXEL_FORMAT_RGB;
    OsSurfaceCanvas canvas;
    assert(os_surface_canvas_init(&canvas, storage + 1, &info) == 0);

    OsUiContext ui;
    assert(os_ui_init(&ui, 0) == 0);
    uint32_t root, indices[8];
    assert(os_ui_add(&ui, OS_UI_WIDGET_CONTAINER, 1, OS_UI_NONE,
                     (OsRect){0,0,64,48},
                     OS_UI_FLAG_VISIBLE | OS_UI_FLAG_ENABLED |
                     OS_UI_FLAG_VERTICAL, &root) == 0);
    const uint32_t types[8] = {OS_UI_WIDGET_LABEL, OS_UI_WIDGET_BUTTON,
        OS_UI_WIDGET_TEXT_FIELD, OS_UI_WIDGET_CHECKBOX, OS_UI_WIDGET_LIST,
        OS_UI_WIDGET_MENU, OS_UI_WIDGET_SCROLL, OS_UI_WIDGET_CONTAINER};
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t flags = OS_UI_FLAG_VISIBLE | OS_UI_FLAG_ENABLED;
        if (i != 0 && i != 7) flags |= OS_UI_FLAG_FOCUSABLE;
        assert(os_ui_add(&ui, types[i], 10 + i, root, (OsRect){0,0,1,1},
                         flags, &indices[i]) == 0);
    }
    assert(os_ui_set_text(&ui, indices[0], "UTF-8: \xED\xA0\x80") == 0);
    assert(os_ui_set_text(&ui, indices[1], "BUTTON") == 0);
    assert(os_ui_set_text(&ui, indices[2], "EDIT") == 0);
    assert(os_ui_set_text(&ui, indices[3], "CHECK") == 0);
    assert(os_ui_set_text(&ui, indices[4], "LIST") == 0);
    assert(os_ui_set_text(&ui, indices[5], "MENU") == 0);
    assert(os_ui_set_text(&ui, indices[6], "SCROLL") == 0);
    assert(os_ui_layout(&ui, root) == 0);
    assert(os_ui_draw(&ui, &canvas) == 0);
    assert(storage[0] == 0xDEADBEEFu && storage[64 * 48 + 1] == 0xC001D00Du);

    OsUiEventResult result;
    click(&ui, indices[1], &result);
    assert(result.action == OS_UI_ACTION_ACTIVATE && result.widget_id == 11);

    click(&ui, indices[4], &result);
    assert(result.action == OS_UI_ACTION_ACTIVATE && result.widget_id == 14);
    click(&ui, indices[5], &result);
    assert(result.action == OS_UI_ACTION_ACTIVATE && result.widget_id == 15);

    OsRect scroll = ui.widgets[indices[6]].rect;
    OsWindowEvent event = pointer(OS_POINTER_EVENT_WHEEL,
                                  scroll.x + 1, scroll.y + 1, 0, 0);
    event.input.data.pointer.wheel_delta = -3;
    assert(os_ui_dispatch(&ui, &event, &result) == 0);
    assert(result.action == OS_UI_ACTION_CHANGE && result.widget_id == 16 &&
           result.value == 3);

    event = key(OS_KEY_TAB, 0);
    assert(os_ui_dispatch(&ui, &event, &result) == 0);
    assert(os_ui_focused(&ui) != OS_UI_NONE);
    ui.focused = indices[2];
    event = key(0x1Eu, 'A');
    assert(os_ui_dispatch(&ui, &event, &result) == 0);
    assert(result.action == OS_UI_ACTION_CHANGE);
    assert(strcmp(ui.widgets[indices[2]].text, "EDITA") == 0);
    ui.focused = indices[3];
    event = key(OS_KEY_SPACE, ' ');
    assert(os_ui_dispatch(&ui, &event, &result) == 0);
    assert(result.action == OS_UI_ACTION_CHANGE && result.value == 1);

    ui.widgets[root].rect = (OsRect){0, 0, 32, 32};
    assert(os_ui_layout(&ui, root) == 0);
    assert(ui.widgets[indices[7]].rect.y + ui.widgets[indices[7]].rect.height == 32);
    os_ui_damage_reset(&ui);
    for (uint32_t i = 0; i <= OS_UI_MAX_DAMAGE; i++)
        assert(os_ui_set_value(&ui, indices[6], (int32_t)i) == 0);
    assert(ui.full_repaint == 1 && ui.damage_count == 0);
    assert(os_ui_add(&ui, OS_UI_WIDGET_BUTTON, 11, root,
                     (OsRect){0,0,1,1}, OS_UI_FLAG_VISIBLE, 0) ==
           OS_ERR_ALREADY_EXISTS);

    uint32_t index = 0, codepoint = 0;
    const char invalid[] = "\xF0\x28\x8C\x28";
    assert(os_utf8_next(invalid, 4, &index, &codepoint) == 0);
    assert(codepoint == OS_UNICODE_REPLACEMENT && index == 1);
    puts("widget/layout/focus/UTF-8 rendering tests OK");
    return 0;
}
'''


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="os64-ui-") as directory:
        source = Path(directory) / "test.c"
        binary = Path(directory) / "test"
        source.write_text(HARNESS)
        subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "user" / "sdk" / "include"),
            str(source), str(ROOT / "user" / "sdk" / "src" / "ui.c"),
            str(ROOT / "user" / "sdk" / "src" / "surface_draw.c"),
            str(ROOT / "user" / "sdk" / "src" / "text.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("public widget SDK source/behavior checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
