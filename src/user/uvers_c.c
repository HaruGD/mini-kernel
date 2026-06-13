#include "userlib.h"

int main(void) {
    user_puts("=== uvers_c.elf ===");
    user_puts("Showing kernel version and uptime from C userland.");
    user_version();
    user_uptime();
    return 0;
}
