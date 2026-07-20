#include <os64/os64.h>

#define GUI_CYCLE_COUNT 4u

static long paint(OsWindow* window, uint32_t cycle, uint32_t stage) {
    OsSurfaceCanvas canvas;
    long result = os_surface_canvas_init(&canvas, window->pixels,
                                         &window->surface_info);
    if (result < 0) {
        return result;
    }
    uint32_t base = OS_RGB(20u + cycle * 9u, 34u + stage * 11u,
                           58u + cycle * 7u);
    uint32_t accent = OS_RGB(80u + stage * 30u, 190u - cycle * 12u,
                             120u + cycle * 15u);
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){0, 0, (int32_t)canvas.width, (int32_t)canvas.height},
        base);
    if (result < 0) {
        return result;
    }
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){12, 12, (int32_t)canvas.width - 24,
                          (int32_t)canvas.height - 24}, accent);
    if (result < 0) {
        return result;
    }
    return os_surface_canvas_draw_text(&canvas, 24, 28, "PHASE 4H SOAK",
                                       OS_RGB(245, 248, 255), accent,
                                       OS_SURFACE_TEXT_TRANSPARENT_BG);
}

int main(void) {
    for (uint32_t cycle = 0; cycle < GUI_CYCLE_COUNT; cycle++) {
        OsWindow window;
        uint32_t width = 220u + cycle * 12u;
        uint32_t height = 150u + cycle * 10u;
        long result = os_window_create(&window, 36 + (int32_t)cycle * 18,
                                       42 + (int32_t)cycle * 14,
                                       width, height);
        if (result < 0) {
            os_printf("[ugui-cycle] create failed cycle=%u result=%ld\n",
                      cycle, result);
            return 1;
        }
        if (paint(&window, cycle, 0) < 0 ||
            os_window_damage_all(&window) < 0 ||
            os_window_move(&window, 70 + (int32_t)cycle * 13,
                           60 + (int32_t)cycle * 9) < 0 ||
            os_window_hide(&window) < 0 || os_window_show(&window) < 0 ||
            os_window_replace_surface(&window) < 0 ||
            paint(&window, cycle, 1) < 0 ||
            os_window_damage_all(&window) < 0 ||
            os_window_resize(&window, width + 24u, height + 16u) < 0 ||
            paint(&window, cycle, 2) < 0 ||
            os_window_damage_all(&window) < 0) {
            os_printf("[ugui-cycle] operation failed cycle=%u\n", cycle);
            os_window_abandon(&window);
            return 1;
        }
        if (os_window_destroy(&window) < 0) {
            os_printf("[ugui-cycle] destroy failed cycle=%u\n", cycle);
            os_window_abandon(&window);
            return 1;
        }
    }
    os_printf("[ugui-cycle] lifecycle OK cycles=%u\n", GUI_CYCLE_COUNT);
    return 0;
}
