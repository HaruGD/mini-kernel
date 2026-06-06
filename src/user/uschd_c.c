#include "userlib.h"

int main(void) {
    user_puts("=== USCHD_C.ELF ===");
    user_puts("Showing scheduler state from C userland.");
    user_sched();
    return 0;
}
