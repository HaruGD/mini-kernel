#include "userlib.h"

int main(void) {
    user_puts("=== uschd_c.elf ===");
    user_puts("Showing scheduler state from C userland.");
    user_sched();
    return 0;
}
