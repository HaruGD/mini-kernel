#include "userlib.h"

int main(void) {
    user_puts("=== umem_c.elf ===");
    user_puts("Showing memory statistics from C userland.");
    user_memstat();
    return 0;
}
