#include "userlib.h"

int main(void) {
    user_puts("=== UMEM_C.ELF ===");
    user_puts("Showing memory statistics from C userland.");
    user_memstat();
    return 0;
}
