#include <os64/os64.h>

int main(void) {
    long pid = os_run("usession_probe_c.elf");
    if (pid < 0 || os_set_background((uint32_t)pid, 1) < 0) {
        os_printf("[session-launch] failed %ld\n", pid);
        return 1;
    }
    os_puts("[session-launch] probe background");
    return 0;
}
