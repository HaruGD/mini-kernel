#include <os64/os64.h>

int main(void) {
    long result = os_run_with_permissions(
        "uimage_c.elf", OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION);
    if (result < 0 || os_set_background(0, 1) < 0) {
        os_printf("[image-ui-launch] failed %ld\n", result);
        return 1;
    }
    os_puts("[image-ui-launch] restricted public-SDK application");
    return 0;
}
