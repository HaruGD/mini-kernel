#include <os64/os64.h>

int main(void) {
    os_puts("=== usleep_c.elf ===");
    os_puts("This is the C version of USLEEP.");
    os_puts("C sleep now: ticks=1000 approx_ms=10000");
    os_sleep(1000);
    os_puts("C wake done");
    return 8;
}
