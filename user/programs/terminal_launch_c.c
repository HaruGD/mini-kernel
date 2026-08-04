#include <os64/os64.h>

int main(int argc, char** argv) {
    uint32_t permissions = OS_PROCESS_PERMISSION_PROFILE_GUI_APPLICATION |
                           OS_PROCESS_PERMISSION_MANAGE_CHILD;
    const char* command = argc == 2 &&
        os_streq(argv[1], "--fault-test")
        ? "terminal.elf --fault-test" : "terminal.elf";
    long result = os_run_with_permissions(command, permissions);
    if (result < 0 || os_set_background(0, 1) < 0) {
        os_printf("[terminal-launch] failed %ld\n", result);
        return 1;
    }
    os_puts("[terminal-launch] GUI terminal started");
    return 0;
}
