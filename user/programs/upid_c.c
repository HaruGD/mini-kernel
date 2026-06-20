#include <os64/os64.h>

int main(void) {
    os_puts("=== upid_c.elf ===");
    os_puts("Showing process identifiers from C userland.");
    os_printf("pid: %x\n", (uint32_t)os_getpid());
    os_printf("ppid: %x\n", (uint32_t)os_getppid());
    return 0;
}
