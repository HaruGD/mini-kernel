#include <os64/os64.h>

int main(void) {
    os_puts("=== uyield_c.elf ===");
    os_puts("Yielding cooperatively three times from C...");

    for (int step = 1; step <= 3; step++) {
        os_printf("Yield step %d\n", step);
        os_yield();
    }

    return 3;
}
