#include "userlib.h"

int main(void) {
    user_puts("=== UPID_C.ELF ===");
    user_puts("Showing process identifiers from C userland.");
    user_printf("pid: %x\n", (uint32_t)user_get_pid());
    user_printf("ppid: %x\n", (uint32_t)user_get_ppid());
    return 0;
}
