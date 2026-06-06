#include "userlib.h"

int main(void) {
    user_puts("=== UBOOT_C.ELF ===");
    user_puts("Showing boot information from C userland.");
    user_bootinfo();
    return 0;
}
