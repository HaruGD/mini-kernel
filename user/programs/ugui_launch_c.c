#include <os64/os64.h>

int main(void) {
    uint32_t permissions = OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION;
    long pid = os_run_with_permissions("ugui_c.elf", permissions);
    if (pid < 0 || os_set_background((uint32_t)pid, 1) < 0) {
        os_printf("[ugui-launch] failed %ld\n", pid);
        return 1;
    }
    os_puts("[ugui-launch] restricted app profile=GUI_APPLICATION permissions=0x25");
    return 0;
}
