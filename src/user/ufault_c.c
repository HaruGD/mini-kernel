#include "userlib.h"

int main(void) {
    volatile unsigned long long* bad = (volatile unsigned long long*)0x0000000080000000ULL;

    user_puts("=== ufault_c.elf ===");
    user_puts("Triggering a user-mode page fault from C...");
    *bad = 0x12345678ULL;
    user_puts("This line should never print.");
    return 0;
}
