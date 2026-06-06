#include "userlib.h"

int main(void) {
    user_puts("=== USLEEP_C.ELF ===");
    user_puts("This is the C version of USLEEP.");
    user_puts("C sleep now");
    user_sleep(1000);
    user_puts("C wake done");
    return 8;
}
