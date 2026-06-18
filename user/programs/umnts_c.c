#include "userlib.h"

int main(void) {
    user_puts("=== umnts_c.elf ===");
    user_puts("Showing VFS mounts from C userland.");
    user_mounts();
    return 0;
}
