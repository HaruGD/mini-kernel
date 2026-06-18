#include "userlib.h"

int main(void) {
    user_puts("=== usleep_c.elf ===");
    user_puts("This is the C version of USLEEP.");
    user_puts("C sleep now: ticks=1000 approx_ms=10000");
    user_sleep(1000);
    user_puts("C wake done");
    return 8;
}
