#include <os64/os64.h>

static OsImage native_image;
static OsImage bmp_image;
static OsImage png_image;
static OsUiContext ui;
static uint32_t root_widget;
static uint32_t widgets[7];

#define IMAGE_UI_BASE_WIDTH 600u
#define IMAGE_UI_BASE_HEIGHT 420u
#define IMAGE_UI_MIN_WIDTH 240u
#define IMAGE_UI_MIN_HEIGHT 180u

static OsRect scale_reference(OsRect reference,
                              uint32_t width,
                              uint32_t height) {
    int64_t left = (int64_t)reference.x * width / IMAGE_UI_BASE_WIDTH;
    int64_t top = (int64_t)reference.y * height / IMAGE_UI_BASE_HEIGHT;
    int64_t right = (int64_t)(reference.x + reference.width) * width /
                    IMAGE_UI_BASE_WIDTH;
    int64_t bottom = (int64_t)(reference.y + reference.height) * height /
                     IMAGE_UI_BASE_HEIGHT;
    if (right <= left) right = left + 1;
    if (bottom <= top) bottom = top + 1;
    if (right > width) right = width;
    if (bottom > height) bottom = height;
    return (OsRect){(int32_t)left, (int32_t)top,
                    (int32_t)(right - left), (int32_t)(bottom - top)};
}

static long layout_ui(uint32_t width, uint32_t height) {
    static const OsRect references[8] = {
        {20, 190, 560, 205},
        {35, 205, 300, 24},
        {35, 240, 120, 30},
        {170, 240, 180, 30},
        {365, 240, 130, 30},
        {35, 285, 145, 70},
        {195, 285, 145, 70},
        {355, 285, 140, 70},
    };
    if (width < OS_WINDOW_MIN_WIDTH || height < OS_WINDOW_MIN_HEIGHT)
        return OS_ERR_OUT_OF_RANGE;
    long result = os_ui_set_rect(&ui, root_widget,
                                 scale_reference(references[0], width, height));
    for (uint32_t i = 0; result == OS_SUCCESS && i < 7; i++)
        result = os_ui_set_rect(&ui, widgets[i],
                                scale_reference(references[i + 1u], width, height));
    return result;
}

static long build_ui(void) {
    long result = os_ui_init(&ui, 0);
    if (result < 0) return result;
    result = os_ui_add(&ui, OS_UI_WIDGET_CONTAINER, 100, OS_UI_NONE,
                       (OsRect){20, 190, 560, 205},
                       OS_UI_FLAG_VISIBLE | OS_UI_FLAG_ENABLED, &root_widget);
    if (result < 0) return result;
    struct Item { uint32_t type; uint32_t id; OsRect rect; const char* text; };
    static const struct Item items[] = {
        {OS_UI_WIDGET_LABEL, 101, {35, 205, 300, 24}, "OS64 public UI: a-z []{} /\\"},
        {OS_UI_WIDGET_BUTTON, 102, {35, 240, 120, 30}, "Button"},
        {OS_UI_WIDGET_TEXT_FIELD, 103, {170, 240, 180, 30}, "edit: "},
        {OS_UI_WIDGET_CHECKBOX, 104, {365, 240, 130, 30}, "Check"},
        {OS_UI_WIDGET_LIST, 105, {35, 285, 145, 70}, "List item"},
        {OS_UI_WIDGET_MENU, 106, {195, 285, 145, 70}, "Menu item"},
        {OS_UI_WIDGET_SCROLL, 107, {355, 285, 140, 70}, "Scroll"},
    };
    for (uint32_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        uint32_t index;
        uint32_t flags = OS_UI_FLAG_VISIBLE | OS_UI_FLAG_ENABLED;
        if (items[i].type != OS_UI_WIDGET_LABEL)
            flags |= OS_UI_FLAG_FOCUSABLE;
        result = os_ui_add(&ui, items[i].type, items[i].id, root_widget,
                           items[i].rect, flags, &index);
        if (result < 0 || os_ui_set_text(&ui, index, items[i].text) < 0)
            return result < 0 ? result : OS_ERR_BAD_BUFFER;
        widgets[i] = index;
    }
    return OS_SUCCESS;
}

static long paint(OsWindow* window) {
    OsSurfaceCanvas canvas;
    long result = os_surface_canvas_init(&canvas, window->pixels,
                                         &window->surface_info);
    if (result < 0) return result;
    result = os_surface_canvas_fill_rect(
        &canvas, (OsRect){0, 0, (int32_t)canvas.width, (int32_t)canvas.height},
        OS_RGB(20, 24, 34));
    if (result < 0) return result;
    result = layout_ui(canvas.width, canvas.height);
    if (result < 0) return result;
    OsRect damage;
    OsRect bounds = scale_reference((OsRect){30, 28, 140, 140},
                                    canvas.width, canvas.height);
    OsRect fitted;
    result = os_image_fit_rect(&native_image, bounds, &fitted);
    if (result < 0) return result;
    result = os_surface_canvas_draw_image(
        &canvas, &native_image,
        (OsRect){0, 0, (int32_t)native_image.width, (int32_t)native_image.height},
        fitted, OS_IMAGE_SCALE_NEAREST, &damage);
    if (result < 0) return result;
    bounds = scale_reference((OsRect){215, 38, 120, 120},
                             canvas.width, canvas.height);
    result = os_image_fit_rect(&bmp_image, bounds, &fitted);
    if (result < 0) return result;
    result = os_surface_canvas_draw_image(
        &canvas, &bmp_image,
        (OsRect){0, 0, (int32_t)bmp_image.width, (int32_t)bmp_image.height},
        fitted, OS_IMAGE_SCALE_NEAREST, &damage);
    if (result < 0) return result;
    bounds = scale_reference((OsRect){380, 28, 140, 140},
                             canvas.width, canvas.height);
    result = os_image_fit_rect(&png_image, bounds, &fitted);
    if (result < 0) return result;
    result = os_surface_canvas_draw_image(
        &canvas, &png_image,
        (OsRect){0, 0, (int32_t)png_image.width, (int32_t)png_image.height},
        fitted, OS_IMAGE_SCALE_NEAREST, &damage);
    if (result < 0) return result;
    return os_ui_draw(&ui, &canvas);
}

static uint32_t pixel_hash(const OsWindow* window) {
    uint32_t hash = 2166136261u;
    for (uint32_t y = 0; y < window->surface_info.height; y += 7u) {
        for (uint32_t x = 0; x < window->surface_info.width; x += 7u) {
            hash ^= window->pixels[y * window->surface_info.stride_pixels + x];
            hash *= 16777619u;
        }
    }
    return hash;
}

static void cleanup(OsWindow* window) {
    os_image_destroy(&native_image);
    os_image_destroy(&bmp_image);
    os_image_destroy(&png_image);
    if (window->window_id != 0) os_window_destroy(window);
}

int main(void) {
    os_image_init(&native_image);
    os_image_init(&bmp_image);
    os_image_init(&png_image);
    if (os_image_load("image_demo.osimg", &native_image) < 0 ||
        os_image_load("image_demo.bmp", &bmp_image) < 0 ||
        os_image_load("image_demo.png", &png_image) < 0) {
        os_puts("[image-ui] decoder load failed");
        cleanup(&(OsWindow){0});
        return 1;
    }
    if (build_ui() < 0) {
        os_puts("[image-ui] widget build failed");
        cleanup(&(OsWindow){0});
        return 1;
    }
    OsWindow window;
    os_window_init(&window);
    long result = os_window_create(&window, 180, 110, 600, 420);
    if (result < 0 || paint(&window) < 0 || os_window_damage_all(&window) < 0 ||
        os_window_focus(&window) < 0) {
        os_printf("[image-ui] initial presentation failed %ld\n", result);
        cleanup(&window);
        return 1;
    }
    os_printf("[image-ui] ready native=%ux%u bmp=%ux%u png=%ux%u widgets=%u hash=0x%x\n",
              native_image.width, native_image.height, bmp_image.width,
              bmp_image.height, png_image.width, png_image.height,
              ui.count, pixel_hash(&window));
    for (;;) {
        OsWindowEvent event;
        result = os_window_wait_event(&window, &event, 0);
        if (result < 0) {
            os_printf("[image-ui] event failed %ld\n", result);
            cleanup(&window);
            return 1;
        }
        if (event.command == OS_WINDOW_EVENT_CLOSE_REQUEST ||
            (event.command == OS_WINDOW_EVENT_KEY &&
             event.input.type == OS_INPUT_EVENT_KEY &&
             event.input.data.key.type == OS_KEY_EVENT_DOWN &&
             event.input.data.key.keycode == OS_KEY_ESCAPE)) {
            cleanup(&window);
            os_puts("[image-ui] cleanup OK");
            return 0;
        }
        if (event.command == OS_WINDOW_EVENT_CONFIGURE) {
            result = os_window_apply_configure(&window);
            if (result < 0) {
                os_printf("[image-ui] configure deferred result=%ld frame=%ux%u surface=%ux%u\n",
                          result, window.frame_width, window.frame_height,
                          window.surface_info.width, window.surface_info.height);
                continue;
            }
            if (paint(&window) < 0 || os_window_damage_all(&window) < 0) {
                cleanup(&window);
                return 1;
            }
            os_printf("[image-ui] configured frame=%ux%u surface=%ux%u compact=%u\n",
                      window.frame_width, window.frame_height,
                      window.surface_info.width, window.surface_info.height,
                      window.frame_width < IMAGE_UI_MIN_WIDTH ||
                      window.frame_height < IMAGE_UI_MIN_HEIGHT);
            continue;
        }
        OsUiEventResult action;
        if (os_ui_dispatch(&ui, &event, &action) < 0) continue;
        if (action.action != OS_UI_ACTION_NONE) {
            os_printf("[image-ui] action=%u widget=%u value=%d\n",
                      action.action, action.widget_id, action.value);
            if (action.widget_id == 103 && action.action == OS_UI_ACTION_CHANGE)
                os_printf("[image-ui] text=%s\n", ui.widgets[widgets[2]].text);
            if (paint(&window) < 0 || os_window_damage_all(&window) < 0) {
                cleanup(&window);
                return 1;
            }
        }
    }
}
