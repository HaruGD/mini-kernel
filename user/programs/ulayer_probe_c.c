#include <os64/os64.h>

int main(void) {
    OsWindow window;
    long desktop = os_window_create_layer(&window, 0, 0, 32, 32,
                                          OS_WINDOW_FLAG_LAYER_DESKTOP);
    if (desktop == OS_SUCCESS) {
        os_window_destroy(&window);
        os_puts("[ulayer] unauthorized desktop accepted");
        return 1;
    }
    long panel = os_window_create_layer(&window, 0, 0, 32, 32,
                                        OS_WINDOW_FLAG_LAYER_PANEL);
    if (panel == OS_SUCCESS) {
        os_window_destroy(&window);
        os_puts("[ulayer] unauthorized panel accepted");
        return 1;
    }
    long overlay = os_window_create_layer(
        &window, 0, 0, 32, 32, OS_WINDOW_FLAG_LAYER_SYSTEM_OVERLAY);
    if (overlay == OS_SUCCESS) {
        os_window_destroy(&window);
        os_puts("[ulayer] unauthorized overlay accepted");
        return 1;
    }
    if (desktop != OS_ERR_PERMISSION_DENIED ||
        panel != OS_ERR_PERMISSION_DENIED ||
        overlay != OS_ERR_PERMISSION_DENIED) {
        os_printf("[ulayer] unexpected results %ld %ld %ld\n",
                  desktop, panel, overlay);
        return 1;
    }
    os_puts("[ulayer] privileged layers denied");
    return 0;
}
