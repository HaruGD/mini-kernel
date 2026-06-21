#include "userlib.h"

int main(void) {
    user_puts("=== ugpfault_c.elf ===");
    user_puts("Triggering a user-mode general protection fault from C...");
    __asm__ volatile("cli");
    user_puts("This line should never print.");
    return 0;
}
