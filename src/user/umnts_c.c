#include "userlib.h"

int main(void) {
    user_puts("=== UMNTS_C.ELF ===");
    user_puts("Showing VFS mounts from C userland.");
    user_mounts();
    return 0;
}
