#include <os64/os64.h>

int main(void) {
    uint32_t permissions = OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION |
                           OS_PROCESS_PERMISSION_MANAGE_CHILD;
    long result = os_run_with_permissions("terminal.elf", permissions);
    if (result < 0 || os_set_background(0, 1) < 0) {
        os_printf("[terminal-launch] failed %ld\n", result);
        return 1;
    }
    os_puts("[terminal-launch] GUI terminal started");
    return 0;
}
