#include <os64/os64.h>

static long paint_window(OsWindow* window, uint32_t theme) {
    OsSurfaceCanvas canvas;
    long result = os_surface_canvas_init(&canvas, window->pixels,
                                         &window->surface_info);
    if (result < 0) {
        return result;
    }
    uint32_t background = theme == 0 ? OS_RGB(24, 36, 61)
                                     : OS_RGB(48, 27, 70);
    uint32_t panel = theme == 0 ? OS_RGB(45, 77, 122)
                                : OS_RGB(88, 45, 104);
    uint32_t accent = theme == 0 ? OS_RGB(54, 211, 153)
                                 : OS_RGB(236, 72, 153);
    uint32_t text = OS_RGB(238, 244, 255);
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){0, 0, (int32_t)canvas.width, (int32_t)canvas.height},
        background);
    if (result < 0) return result;
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){24, 24, (int32_t)canvas.width - 48,
                          (int32_t)canvas.height - 48}, panel);
    if (result < 0) return result;
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){24, 24, 8, (int32_t)canvas.height - 48}, accent);
    if (result < 0) return result;
    result = os_surface_canvas_draw_text(&canvas, 52, 52, "OS64 WINDOW SDK",
                                         text, panel,
                                         OS_SURFACE_TEXT_TRANSPARENT_BG);
    if (result < 0) return result;
    result = os_surface_canvas_draw_text(
        &canvas, 52, 76,
        theme == 0 ? "F1 REDRAW  F2 RESIZE  ESC EXIT"
                   : "REDRAW OK  F2 RESIZE  ESC EXIT",
        accent, panel, OS_SURFACE_TEXT_TRANSPARENT_BG);
    if (result < 0) return result;
    return os_surface_canvas_draw_line(
        &canvas, 52, 100, (int32_t)canvas.width - 52, 100, accent);
}

static int permission_boundary_ok(void) {
    OsInputEvent event;
    OsGraphicsInfo graphics;
    long input_result = os_input_poll(&event);
    long display_result = os_gfx_get_info(&graphics);
    if (input_result != OS_ERR_PERMISSION_DENIED ||
        display_result != OS_ERR_PERMISSION_DENIED) {
        os_printf("[ugui] permission boundary failed input=%ld display=%ld\n",
                  input_result, display_result);
        return 0;
    }
    os_printf("[ugui] permission boundary OK input=%ld display=%ld\n",
              input_result, display_result);
    return 1;
}

int main(void) {
    if (!permission_boundary_ok()) {
        return 1;
    }

    OsWindow window;
    long result = os_window_create(&window, 120, 100, 360, 240);
    if (result < 0) {
        os_printf("[ugui] create failed %ld\n", result);
        return 1;
    }
    result = paint_window(&window, 0);
    if (result < 0 || os_window_damage_all(&window) < 0) {
        os_printf("[ugui] initial draw failed %ld\n", result);
        os_window_destroy(&window);
        return 1;
    }
    OsWindowInfo info;
    if (os_window_get_info(&window, &info) < 0) {
        os_puts("[ugui] information failed");
        os_window_destroy(&window);
        return 1;
    }
    os_printf("[ugui] initial frame id=%u generation=%u size=%ux%u\n",
              info.window_id, info.window_generation, info.width, info.height);
    if (os_window_focus(&window) < 0) {
        os_puts("[ugui] focus request failed");
        os_window_destroy(&window);
        return 1;
    }

    uint32_t last_sequence = 0;
    int focused = 0;
    while (!focused) {
        OsWindowEvent event;
        result = os_window_wait_event(&window, &event, 5000);
        if (result < 0 || event.event_sequence <= last_sequence) {
            os_puts("[ugui] focus event failed");
            os_window_destroy(&window);
            return 1;
        }
        last_sequence = event.event_sequence;
        focused = event.command == OS_WINDOW_EVENT_FOCUS_IN;
    }
    os_puts("[ugui] focused ready");

    while (1) {
        OsWindowEvent event;
        result = os_window_wait_event(&window, &event, 0);
        if (result < 0 || event.event_sequence <= last_sequence) {
            os_printf("[ugui] event failure %ld\n", result);
            os_window_destroy(&window);
            return 1;
        }
        last_sequence = event.event_sequence;
        if (event.command != OS_WINDOW_EVENT_KEY ||
            event.input.type != OS_INPUT_EVENT_KEY ||
            event.input.data.key.type != OS_KEY_EVENT_DOWN) {
            continue;
        }

        uint32_t key = event.input.data.key.keycode;
        if (key == OS_KEY_F1) {
            long replace_result = os_window_replace_surface(&window);
            long paint_result = replace_result < 0
                ? OS_ERR_NOT_READY : paint_window(&window, 1);
            if (replace_result < 0 || paint_result < 0) {
                os_printf("[ugui] redraw surface failed replace=%ld paint=%ld\n",
                          replace_result, paint_result);
                os_window_destroy(&window);
                return 1;
            }
            OsRect rects[6] = {
                {0, 0, 180, 120}, {180, 0, 180, 120},
                {0, 120, 180, 120}, {180, 120, 180, 120},
                {24, 24, 8, 192}, {52, 52, 240, 56},
            };
            if (os_window_damage(&window, rects, 6) < 0) {
                os_puts("[ugui] redraw damage failed");
                os_window_destroy(&window);
                return 1;
            }
            os_puts("[ugui] redraw key=F1 rects=6");
        } else if (key == OS_KEY_F2) {
            if (os_window_resize(&window, 420, 280) < 0 ||
                paint_window(&window, 1) < 0 ||
                os_window_damage_all(&window) < 0) {
                os_puts("[ugui] resize failed");
                os_window_destroy(&window);
                return 1;
            }
            if (os_window_get_info(&window, &info) < 0 ||
                info.width != 420 || info.height != 280) {
                os_puts("[ugui] resized information failed");
                os_window_destroy(&window);
                return 1;
            }
            os_puts("[ugui] resized 420x280");
        } else if (key == OS_KEY_ESCAPE) {
            if (os_window_destroy(&window) < 0) {
                os_puts("[ugui] destroy failed");
                return 1;
            }
            os_puts("[ugui] lifecycle OK");
            return 0;
        }
    }
}
