#include "userlib.h"

static char pid_nibble_char(uint32_t pid) {
    uint32_t nibble = pid & 0x0Fu;
    if (nibble <= 9u) {
        return (char)('0' + nibble);
    }
    return (char)('A' + (nibble - 10u));
}

int main(void) {
    uint32_t pid = (uint32_t)user_get_pid();
    char pid_char = pid_nibble_char(pid);

    user_puts("=== UBUSY_C.ELF ===");
    user_puts("Preemptive scheduling C demo: one setup yield, then busy work without sys_yield.");
    user_printf("Busy worker pid nibble=%c\n", pid_char);
    user_yield();

    for (int step = 1; step <= 3; step++) {
        volatile uint32_t spin = 0x02000000u;
        while (spin-- != 0u) {
        }

        user_printf("Busy pid %c step %d\n", pid_char, step);
    }

    return (int)(unsigned char)pid_char;
}
