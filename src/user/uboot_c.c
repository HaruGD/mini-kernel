#include "userlib.h"

int main(void) {
    user_puts("=== uboot_c.elf ===");
    user_puts("Showing boot information from C userland.");
    user_bootinfo();
    return 0;
}
