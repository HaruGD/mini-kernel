#include "userlib.h"

int main(void) {
    user_puts("=== uyield_c.elf ===");
    user_puts("Yielding cooperatively three times from C...");

    for (int step = 1; step <= 3; step++) {
        user_printf("Yield step %d\n", step);
        user_yield();
    }

    return 3;
}
